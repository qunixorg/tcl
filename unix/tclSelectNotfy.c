/*
 * tclSelectNotfy.c --
 *
 *	This file contains the implementation of the select()-based generic
 *	Unix notifier, which is the lowest-level part of the Tcl event loop.
 *	This file works together with generic/tclNotify.c.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tclInt.h"
#ifndef HAVE_COREFOUNDATION	/* Darwin/Mac OS X CoreFoundation notifier is
				 * in tclMacOSXNotify.c */
#if (!defined(NOTIFIER_EPOLL) && !defined(NOTIFIER_KQUEUE)) || !TCL_THREADS

#include <signal.h>

/*
 * This structure is used to keep track of the notifier info for a registered
 * file.
 */

typedef struct FileHandler {
    int fd;
    int mask;			/* Mask of desired events: TCL_READABLE,
				 * etc. */
    int readyMask;		/* Mask of events that have been seen since
				 * the last time file handlers were invoked
				 * for this file. */
    Tcl_FileProc *proc;		/* Function to call, in the style of
				 * Tcl_CreateFileHandler. */
    ClientData clientData;	/* Argument to pass to proc. */
    struct FileHandler *nextPtr;/* Next in list of all files we care about. */
} FileHandler;

/*
 * The following structure contains a set of select() masks to track readable,
 * writable, and exception conditions.
 */

typedef struct {
    fd_set readable;
    fd_set writable;
    fd_set exception;
} SelectMasks;

/*
 * The following structure is what is added to the Tcl event queue when file
 * handlers are ready to fire.
 */

typedef struct {
    Tcl_Event header;		/* Information that is standard for all
				 * events. */
    int fd;			/* File descriptor that is ready. Used to find
				 * the FileHandler structure for the file
				 * (can't point directly to the FileHandler
				 * structure because it could go away while
				 * the event is queued). */
} FileHandlerEvent;

/*
 * The following static structure contains the state information for the
 * select based implementation of the Tcl notifier. One of these structures is
 * created for each thread that is using the notifier.
 */

typedef struct ThreadSpecificData {
    FileHandler *firstFileHandlerPtr;
				/* Pointer to head of file handler list. */
    SelectMasks checkMasks;	/* This structure is used to build up the
				 * masks to be used in the next call to
				 * select. Bits are set in response to calls
				 * to Tcl_CreateFileHandler. */
    SelectMasks readyMasks;	/* This array reflects the readable/writable
				 * conditions that were found to exist by the
				 * last call to select. */
    int numFdBits;		/* Number of valid bits in checkMasks (one
				 * more than highest fd for which
				 * Tcl_WatchFile has been called). */
#if TCL_THREADS
    int onList;			/* True if it is in this list */
    unsigned int pollState;	/* pollState is used to implement a polling
				 * handshake between each thread and the
				 * notifier thread. Bits defined below. */
    struct ThreadSpecificData *nextPtr, *prevPtr;
				/* All threads that are currently waiting on
				 * an event have their ThreadSpecificData
				 * structure on a doubly-linked listed formed
				 * from these pointers. You must hold the
				 * notifierMutex lock before accessing these
				 * fields. */
#ifdef __CYGWIN__
    void *event;		/* Any other thread alerts a notifier that an
				 * event is ready to be processed by sending
				 * this event. */
    void *hwnd;			/* Messaging window. */
#else /* !__CYGWIN__ */
    pthread_cond_t waitCV;	/* Any other thread alerts a notifier that an
				 * event is ready to be processed by signaling
				 * this condition variable. */
#endif /* __CYGWIN__ */
    int waitCVinitialized;	/* Variable to flag initialization of the
				 * structure. */
    int eventReady;		/* True if an event is ready to be processed.
				 * Used as condition flag together with waitCV
				 * above. */
#endif /* TCL_THREADS */
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#if TCL_THREADS
/*
 * The following static indicates the number of threads that have initialized
 * notifiers.
 *
 * You must hold the notifierMutex lock before accessing this variable.
 */

static int notifierCount = 0;

/*
 * The following variable points to the head of a doubly-linked list of
 * ThreadSpecificData structures for all threads that are currently waiting on
 * an event.
 *
 * You must hold the notifierMutex lock before accessing this list.
 */

static ThreadSpecificData *waitingListPtr = NULL;

/*
 * The notifier thread spends all its time in select() waiting for a file
 * descriptor associated with one of the threads on the waitingListPtr list to
 * do something interesting. But if the contents of the waitingListPtr list
 * ever changes, we need to wake up and restart the select() system call. You
 * can wake up the notifier thread by writing a single byte to the file
 * descriptor defined below. This file descriptor is the input-end of a pipe
 * and the notifier thread is listening for data on the output-end of the same
 * pipe. Hence writing to this file descriptor will cause the select() system
 * call to return and wake up the notifier thread.
 *
 * You must hold the notifierMutex lock before writing to the pipe.
 */

static int triggerPipe = -1;

/*
 * The notifierMutex locks access to all of the global notifier state.
 */

static pthread_mutex_t notifierInitMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t notifierMutex     = PTHREAD_MUTEX_INITIALIZER;
/*
 * The following static indicates if the notifier thread is running.
 *
 * You must hold the notifierInitMutex before accessing this variable.
 */

static int notifierThreadRunning = 0;

/*
 * The notifier thread signals the notifierCV when it has finished
 * initializing the triggerPipe and right before the notifier thread
 * terminates.
 */

static pthread_cond_t notifierCV = PTHREAD_COND_INITIALIZER;

/*
 * The pollState bits:
 *
 * POLL_WANT is set by each thread before it waits on its condition variable.
 *	It is checked by the notifier before it does select.
 *
 * POLL_DONE is set by the notifier if it goes into select after seeing
 *	POLL_WANT. The idea is to ensure it tries a select with the same bits
 *	the initial thread had set.
 */

#define POLL_WANT	0x1
#define POLL_DONE	0x2

/*
 * This is the thread ID of the notifier thread that does select.
 */

static Tcl_ThreadId notifierThread;
#endif /* TCL_THREADS */

/*
 * Static routines defined in this file.
 */

#if TCL_THREADS
static TCL_NORETURN void NotifierThreadProc(ClientData clientData);
#if defined(HAVE_PTHREAD_ATFORK)
static int atForkInit = 0;
static void		AtForkChild(void);
#endif /* HAVE_PTHREAD_ATFORK */
#endif /* TCL_THREADS */
static int		FileHandlerEventProc(Tcl_Event *evPtr, int flags);

/*
 * Import of critical bits of Windows API when building threaded with Cygwin.
 */

#if defined(__CYGWIN__)
typedef struct {
    void *hwnd;			/* Messaging window. */
    unsigned int *message;	/* Message payload. */
    int wParam;			/* Event-specific "word" parameter. */
    int lParam;			/* Event-specific "long" parameter. */
    int time;			/* Event timestamp. */
    int x;			/* Event location (where meaningful). */
    int y;
} MSG;

typedef struct {
    unsigned int style;
    void *lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    void *hInstance;
    void *hIcon;
    void *hCursor;
    void *hbrBackground;
    void *lpszMenuName;
    const void *lpszClassName;
} WNDCLASSW;

extern void __stdcall	CloseHandle(void *);
extern void *__stdcall	CreateEventW(void *, unsigned char, unsigned char,
			    void *);
extern void *__stdcall	CreateWindowExW(void *, const void *, const void *,
			    DWORD, int, int, int, int, void *, void *, void *,
			    void *);
extern DWORD __stdcall	DefWindowProcW(void *, int, void *, void *);
extern unsigned char __stdcall	DestroyWindow(void *);
extern int __stdcall	DispatchMessageW(const MSG *);
extern unsigned char __stdcall	GetMessageW(MSG *, void *, int, int);
extern void __stdcall	MsgWaitForMultipleObjects(DWORD, void *,
			    unsigned char, DWORD, DWORD);
extern unsigned char __stdcall	PeekMessageW(MSG *, void *, int, int, int);
extern unsigned char __stdcall	PostMessageW(void *, unsigned int, void *,
				    void *);
extern void __stdcall	PostQuitMessage(int);
extern void *__stdcall	RegisterClassW(const WNDCLASSW *);
extern unsigned char __stdcall	ResetEvent(void *);
extern unsigned char __stdcall	TranslateMessage(const MSG *);

/*
 * Threaded-cygwin specific constants and functions in this file:
 */

static const WCHAR className[] = L"TclNotifier";
static DWORD __stdcall	NotifierProc(void *hwnd, unsigned int message,
			    void *wParam, void *lParam);
#endif /* TCL_THREADS && __CYGWIN__ */


#include "tclUnixNotfy.c"

/*
 *----------------------------------------------------------------------
 *
 * Tcl_InitNotifier --
 *
 *	Initializes the platform specific notifier state.
 *
 * Results:
 *	Returns a handle to the notifier state for this thread.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ClientData
Tcl_InitNotifier(void)
{
    if (tclNotifierHooks.initNotifierProc) {
	return tclNotifierHooks.initNotifierProc();
    } else {
	ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

#if TCL_THREADS
	tsdPtr->eventReady = 0;

	/*
	 * Initialize thread specific condition variable for this thread.
	 */
	if (tsdPtr->waitCVinitialized == 0) {
#ifdef __CYGWIN__
	    WNDCLASSW class;

	    class.style = 0;
	    class.cbClsExtra = 0;
	    class.cbWndExtra = 0;
	    class.hInstance = TclWinGetTclInstance();
	    class.hbrBackground = NULL;
	    class.lpszMenuName = NULL;
	    class.lpszClassName = className;
	    class.lpfnWndProc = NotifierProc;
	    class.hIcon = NULL;
	    class.hCursor = NULL;

	    RegisterClassW(&class);
	    tsdPtr->hwnd = CreateWindowExW(NULL, class.lpszClassName,
		    class.lpszClassName, 0, 0, 0, 0, 0, NULL, NULL,
		    TclWinGetTclInstance(), NULL);
	    tsdPtr->event = CreateEventW(NULL, 1 /* manual */,
		    0 /* !signaled */, NULL);
#else
	    pthread_cond_init(&tsdPtr->waitCV, NULL);
#endif /* __CYGWIN__ */
	    tsdPtr->waitCVinitialized = 1;
	}

	pthread_mutex_lock(&notifierInitMutex);
#if defined(HAVE_PTHREAD_ATFORK)
	/*
	 * Install pthread_atfork handlers to clean up the notifier in the
	 * child of a fork.
	 */

	if (!atForkInit) {
	    int result = pthread_atfork(NULL, NULL, AtForkChild);

	    if (result) {
		Tcl_Panic("Tcl_InitNotifier: pthread_atfork failed");
	    }
	    atForkInit = 1;
	}
#endif /* HAVE_PTHREAD_ATFORK */

	notifierCount++;
	pthread_mutex_unlock(&notifierInitMutex);

#endif /* TCL_THREADS */
	return tsdPtr;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_FinalizeNotifier --
 *
 *	This function is called to cleanup the notifier state before a thread
 *	is terminated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May terminate the background notifier thread if this is the last
 *	notifier instance.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_FinalizeNotifier(
    ClientData clientData)		/* Not used. */
{
    if (tclNotifierHooks.finalizeNotifierProc) {
	tclNotifierHooks.finalizeNotifierProc(clientData);
	return;
    } else {
#if TCL_THREADS
	ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

	pthread_mutex_lock(&notifierInitMutex);
	notifierCount--;

	/*
	 * If this is the last thread to use the notifier, close the notifier
	 * pipe and wait for the background thread to terminate.
	 */

	if (notifierCount == 0 && triggerPipe != -1) {
	    if (write(triggerPipe, "q", 1) != 1) {
		Tcl_Panic("Tcl_FinalizeNotifier: %s",
			"unable to write 'q' to triggerPipe");
	    }
	    close(triggerPipe);
	    pthread_mutex_lock(&notifierMutex);
	    while(triggerPipe != -1) {
		pthread_cond_wait(&notifierCV, &notifierMutex);
	    }
	    pthread_mutex_unlock(&notifierMutex);
	    if (notifierThreadRunning) {
		int result = pthread_join((pthread_t) notifierThread, NULL);

		if (result) {
		    Tcl_Panic("Tcl_FinalizeNotifier: %s",
			    "unable to join notifier thread");
		}
		notifierThreadRunning = 0;
	    }
	}

	/*
	 * Clean up any synchronization objects in the thread local storage.
	 */

#ifdef __CYGWIN__
	DestroyWindow(tsdPtr->hwnd);
	CloseHandle(tsdPtr->event);
#else /* __CYGWIN__ */
	pthread_cond_destroy(&tsdPtr->waitCV);
#endif /* __CYGWIN__ */
	tsdPtr->waitCVinitialized = 0;

	pthread_mutex_unlock(&notifierInitMutex);
#endif /* TCL_THREADS */
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_CreateFileHandler --
 *
 *	This function registers a file handler with the select notifier.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a new file handler structure.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_CreateFileHandler(
    int fd,			/* Handle of stream to watch. */
    int mask,			/* OR'ed combination of TCL_READABLE,
				 * TCL_WRITABLE, and TCL_EXCEPTION: indicates
				 * conditions under which proc should be
				 * called. */
    Tcl_FileProc *proc,		/* Function to call for each selected
				 * event. */
    ClientData clientData)	/* Arbitrary data to pass to proc. */
{
    if (tclNotifierHooks.createFileHandlerProc) {
	tclNotifierHooks.createFileHandlerProc(fd, mask, proc, clientData);
	return;
    } else {
	ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
	FileHandler *filePtr;

	for (filePtr = tsdPtr->firstFileHandlerPtr; filePtr != NULL;
		filePtr = filePtr->nextPtr) {
	    if (filePtr->fd == fd) {
		break;
	    }
	}
	if (filePtr == NULL) {
	    filePtr = Tcl_Alloc(sizeof(FileHandler));
	    filePtr->fd = fd;
	    filePtr->readyMask = 0;
	    filePtr->nextPtr = tsdPtr->firstFileHandlerPtr;
	    tsdPtr->firstFileHandlerPtr = filePtr;
	}
	filePtr->proc = proc;
	filePtr->clientData = clientData;
	filePtr->mask = mask;

	/*
	 * Update the check masks for this file.
	 */

	if (mask & TCL_READABLE) {
	    FD_SET(fd, &tsdPtr->checkMasks.readable);
	} else {
	    FD_CLR(fd, &tsdPtr->checkMasks.readable);
	}
	if (mask & TCL_WRITABLE) {
	    FD_SET(fd, &tsdPtr->checkMasks.writable);
	} else {
	    FD_CLR(fd, &tsdPtr->checkMasks.writable);
	}
	if (mask & TCL_EXCEPTION) {
	    FD_SET(fd, &tsdPtr->checkMasks.exception);
	} else {
	    FD_CLR(fd, &tsdPtr->checkMasks.exception);
	}
	if (tsdPtr->numFdBits <= fd) {
	    tsdPtr->numFdBits = fd+1;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_DeleteFileHandler --
 *
 *	Cancel a previously-arranged callback arrangement for a file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If a callback was previously registered on file, remove it.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_DeleteFileHandler(
    int fd)			/* Stream id for which to remove callback
				 * function. */
{
    if (tclNotifierHooks.deleteFileHandlerProc) {
	tclNotifierHooks.deleteFileHandlerProc(fd);
	return;
    } else {
	FileHandler *filePtr, *prevPtr;
	int i;
	ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

	/*
	 * Find the entry for the given file (and return if there isn't one).
	 */

	for (prevPtr = NULL, filePtr = tsdPtr->firstFileHandlerPtr; ;
		prevPtr = filePtr, filePtr = filePtr->nextPtr) {
	    if (filePtr == NULL) {
		return;
	    }
	    if (filePtr->fd == fd) {
		break;
	    }
	}

	/*
	 * Update the check masks for this file.
	 */

	if (filePtr->mask & TCL_READABLE) {
	    FD_CLR(fd, &tsdPtr->checkMasks.readable);
	}
	if (filePtr->mask & TCL_WRITABLE) {
	    FD_CLR(fd, &tsdPtr->checkMasks.writable);
	}
	if (filePtr->mask & TCL_EXCEPTION) {
	    FD_CLR(fd, &tsdPtr->checkMasks.exception);
	}

	/*
	 * Find current max fd.
	 */

	if (fd+1 == tsdPtr->numFdBits) {
	    int numFdBits = 0;

	    for (i = fd-1; i >= 0; i--) {
		if (FD_ISSET(i, &tsdPtr->checkMasks.readable)
			|| FD_ISSET(i, &tsdPtr->checkMasks.writable)
			|| FD_ISSET(i, &tsdPtr->checkMasks.exception)) {
		    numFdBits = i+1;
		    break;
		}
	    }
	    tsdPtr->numFdBits = numFdBits;
	}

	/*
	 * Clean up information in the callback record.
	 */

	if (prevPtr == NULL) {
	    tsdPtr->firstFileHandlerPtr = filePtr->nextPtr;
	} else {
	    prevPtr->nextPtr = filePtr->nextPtr;
	}
	Tcl_Free(filePtr);
    }
}

#if defined(__CYGWIN__)

static DWORD __stdcall
NotifierProc(
    void *hwnd,
    unsigned int message,
    void *wParam,
    void *lParam)
{
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    if (message != 1024) {
	return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    /*
     * Process all of the runnable events.
     */

    tsdPtr->eventReady = 1;
    Tcl_ServiceAll();
    return 0;
}
#endif /* TCL_THREADS && __CYGWIN__ */

/*
 *----------------------------------------------------------------------
 *
 * Tcl_WaitForEvent --
 *
 *	This function is called by Tcl_DoOneEvent to wait for new events on
 *	the message queue. If the block time is 0, then Tcl_WaitForEvent just
 *	polls without blocking.
 *
 * Results:
 *	Returns -1 if the select would block forever, otherwise returns 0.
 *
 * Side effects:
 *	Queues file events that are detected by the select.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_WaitForEvent(
    const Tcl_Time *timePtr)		/* Maximum block time, or NULL. */
{
    if (tclNotifierHooks.waitForEventProc) {
	return tclNotifierHooks.waitForEventProc(timePtr);
    } else {
	FileHandler *filePtr;
	int mask;
	Tcl_Time vTime;
#if TCL_THREADS
	int waitForFiles;
#   ifdef __CYGWIN__
	MSG msg;
#   endif /* __CYGWIN__ */
#else /* !TCL_THREADS */
	/*
	 * Impl. notes: timeout & timeoutPtr are used if, and only if threads
	 * are not enabled. They are the arguments for the regular select()
	 * used when the core is not thread-enabled.
	 */

	struct timeval timeout, *timeoutPtr;
	int numFound;
#endif /* TCL_THREADS */
	ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

	/*
	 * Set up the timeout structure. Note that if there are no events to
	 * check for, we return with a negative result rather than blocking
	 * forever.
	 */

	if (timePtr != NULL) {
	    /*
	     * TIP #233 (Virtualized Time). Is virtual time in effect? And do
	     * we actually have something to scale? If yes to both then we
	     * call the handler to do this scaling.
	     */

	    if (timePtr->sec != 0 || timePtr->usec != 0) {
		vTime = *timePtr;
		tclScaleTimeProcPtr(&vTime, tclTimeClientData);
		timePtr = &vTime;
	    }
#if !TCL_THREADS
	    timeout.tv_sec = timePtr->sec;
	    timeout.tv_usec = timePtr->usec;
	    timeoutPtr = &timeout;
	} else if (tsdPtr->numFdBits == 0) {
	    /*
	     * If there are no threads, no timeout, and no fds registered,
	     * then there are no events possible and we must avoid deadlock.
	     * Note that this is not entirely correct because there might be a
	     * signal that could interrupt the select call, but we don't
	     * handle that case if we aren't using threads.
	     */

	    return -1;
	} else {
	    timeoutPtr = NULL;
#endif /* !TCL_THREADS */
	}

#if TCL_THREADS
	/*
	 * Start notifier thread and place this thread on the list of
	 * interested threads, signal the notifier thread, and wait for a
	 * response or a timeout.
	 */
	StartNotifierThread("Tcl_WaitForEvent");

	pthread_mutex_lock(&notifierMutex);

	if (timePtr != NULL && timePtr->sec == 0 && (timePtr->usec == 0
#if defined(__APPLE__) && defined(__LP64__)
		/*
		 * On 64-bit Darwin, pthread_cond_timedwait() appears to have
		 * a bug that causes it to wait forever when passed an
		 * absolute time which has already been exceeded by the system
		 * time; as a workaround, when given a very brief timeout,
		 * just do a poll. [Bug 1457797]
		 */
		|| timePtr->usec < 10
#endif /* __APPLE__ && __LP64__ */
		)) {
	    /*
	     * Cannot emulate a polling select with a polling condition
	     * variable. Instead, pretend to wait for files and tell the
	     * notifier thread what we are doing. The notifier thread makes
	     * sure it goes through select with its select mask in the same
	     * state as ours currently is. We block until that happens.
	     */

	    waitForFiles = 1;
	    tsdPtr->pollState = POLL_WANT;
	    timePtr = NULL;
	} else {
	    waitForFiles = (tsdPtr->numFdBits > 0);
	    tsdPtr->pollState = 0;
	}

	if (waitForFiles) {
	    /*
	     * Add the ThreadSpecificData structure of this thread to the list
	     * of ThreadSpecificData structures of all threads that are
	     * waiting on file events.
	     */

	    tsdPtr->nextPtr = waitingListPtr;
	    if (waitingListPtr) {
		waitingListPtr->prevPtr = tsdPtr;
	    }
	    tsdPtr->prevPtr = 0;
	    waitingListPtr = tsdPtr;
	    tsdPtr->onList = 1;

	    if ((write(triggerPipe, "", 1) == -1) && (errno != EAGAIN)) {
		Tcl_Panic("Tcl_WaitForEvent: %s",
			"unable to write to triggerPipe");
	    }
	}

	FD_ZERO(&tsdPtr->readyMasks.readable);
	FD_ZERO(&tsdPtr->readyMasks.writable);
	FD_ZERO(&tsdPtr->readyMasks.exception);

	if (!tsdPtr->eventReady) {
#ifdef __CYGWIN__
	    if (!PeekMessageW(&msg, NULL, 0, 0, 0)) {
		DWORD timeout;

		if (timePtr) {
		    timeout = timePtr->sec * 1000 + timePtr->usec / 1000;
		} else {
		    timeout = 0xFFFFFFFF;
		}
		pthread_mutex_unlock(&notifierMutex);
		MsgWaitForMultipleObjects(1, &tsdPtr->event, 0, timeout, 1279);
		pthread_mutex_lock(&notifierMutex);
	    }
#else /* !__CYGWIN__ */
	    if (timePtr != NULL) {
		Tcl_Time now;
		struct timespec ptime;

		Tcl_GetTime(&now);
		ptime.tv_sec = timePtr->sec + now.sec +
			(timePtr->usec + now.usec) / 1000000;
		ptime.tv_nsec = 1000 * ((timePtr->usec + now.usec) % 1000000);

		pthread_cond_timedwait(&tsdPtr->waitCV, &notifierMutex, &ptime);
	    } else {
		pthread_cond_wait(&tsdPtr->waitCV, &notifierMutex);
	    }
#endif /* __CYGWIN__ */
	}
	tsdPtr->eventReady = 0;

#ifdef __CYGWIN__
	while (PeekMessageW(&msg, NULL, 0, 0, 0)) {
	    /*
	     * Retrieve and dispatch the message.
	     */

	    DWORD result = GetMessageW(&msg, NULL, 0, 0);

	    if (result == 0) {
		PostQuitMessage(msg.wParam);
		/* What to do here? */
	    } else if (result != (DWORD) -1) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	    }
	}
	ResetEvent(tsdPtr->event);
#endif /* __CYGWIN__ */

	if (waitForFiles && tsdPtr->onList) {
	    /*
	     * Remove the ThreadSpecificData structure of this thread from the
	     * waiting list. Alert the notifier thread to recompute its select
	     * masks - skipping this caused a hang when trying to close a pipe
	     * which the notifier thread was still doing a select on.
	     */

	    if (tsdPtr->prevPtr) {
		tsdPtr->prevPtr->nextPtr = tsdPtr->nextPtr;
	    } else {
		waitingListPtr = tsdPtr->nextPtr;
	    }
	    if (tsdPtr->nextPtr) {
		tsdPtr->nextPtr->prevPtr = tsdPtr->prevPtr;
	    }
	    tsdPtr->nextPtr = tsdPtr->prevPtr = NULL;
	    tsdPtr->onList = 0;
	    if ((write(triggerPipe, "", 1) == -1) && (errno != EAGAIN)) {
		Tcl_Panic("Tcl_WaitForEvent: %s",
			"unable to write to triggerPipe");
	    }
	}
#else /* !TCL_THREADS */
	tsdPtr->readyMasks = tsdPtr->checkMasks;
	numFound = select(tsdPtr->numFdBits, &tsdPtr->readyMasks.readable,
		&tsdPtr->readyMasks.writable, &tsdPtr->readyMasks.exception,
		timeoutPtr);

	/*
	 * Some systems don't clear the masks after an error, so we have to do
	 * it here.
	 */

	if (numFound == -1) {
	    FD_ZERO(&tsdPtr->readyMasks.readable);
	    FD_ZERO(&tsdPtr->readyMasks.writable);
	    FD_ZERO(&tsdPtr->readyMasks.exception);
	}
#endif /* TCL_THREADS */

	/*
	 * Queue all detected file events before returning.
	 */

	for (filePtr = tsdPtr->firstFileHandlerPtr; (filePtr != NULL);
		filePtr = filePtr->nextPtr) {
	    mask = 0;
	    if (FD_ISSET(filePtr->fd, &tsdPtr->readyMasks.readable)) {
		mask |= TCL_READABLE;
	    }
	    if (FD_ISSET(filePtr->fd, &tsdPtr->readyMasks.writable)) {
		mask |= TCL_WRITABLE;
	    }
	    if (FD_ISSET(filePtr->fd, &tsdPtr->readyMasks.exception)) {
		mask |= TCL_EXCEPTION;
	    }

	    if (!mask) {
		continue;
	    }

	    /*
	     * Don't bother to queue an event if the mask was previously
	     * non-zero since an event must still be on the queue.
	     */

	    if (filePtr->readyMask == 0) {
		FileHandlerEvent *fileEvPtr =
			Tcl_Alloc(sizeof(FileHandlerEvent));

		fileEvPtr->header.proc = FileHandlerEventProc;
		fileEvPtr->fd = filePtr->fd;
		Tcl_QueueEvent((Tcl_Event *) fileEvPtr, TCL_QUEUE_TAIL);
	    }
	    filePtr->readyMask = mask;
	}
#if TCL_THREADS
	pthread_mutex_unlock(&notifierMutex);
#endif /* TCL_THREADS */
	return 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * NotifierThreadProc --
 *
 *	This routine is the initial (and only) function executed by the
 *	special notifier thread. Its job is to wait for file descriptors to
 *	become readable or writable or to have an exception condition and then
 *	to notify other threads who are interested in this information by
 *	signalling a condition variable. Other threads can signal this
 *	notifier thread of a change in their interests by writing a single
 *	byte to a special pipe that the notifier thread is monitoring.
 *
 * Result:
 *	None. Once started, this routine never exits. It dies with the overall
 *	process.
 *
 * Side effects:
 *	The trigger pipe used to signal the notifier thread is created when
 *	the notifier thread first starts.
 *
 *----------------------------------------------------------------------
 */

#if TCL_THREADS
static TCL_NORETURN void
NotifierThreadProc(
    ClientData clientData)	/* Not used. */
{
    ThreadSpecificData *tsdPtr;
    fd_set readableMask;
    fd_set writableMask;
    fd_set exceptionMask;
    int i;
    int fds[2], receivePipe;
    long found;
    struct timeval poll = {0., 0.}, *timePtr;
    char buf[2];
    int numFdBits = 0;

    if (pipe(fds) != 0) {
	Tcl_Panic("NotifierThreadProc: %s", "could not create trigger pipe");
    }

    receivePipe = fds[0];

    if (TclUnixSetBlockingMode(receivePipe, TCL_MODE_NONBLOCKING) < 0) {
	Tcl_Panic("NotifierThreadProc: %s",
		"could not make receive pipe non blocking");
    }
    if (TclUnixSetBlockingMode(fds[1], TCL_MODE_NONBLOCKING) < 0) {
	Tcl_Panic("NotifierThreadProc: %s",
		"could not make trigger pipe non blocking");
    }
    if (fcntl(receivePipe, F_SETFD, FD_CLOEXEC) < 0) {
	Tcl_Panic("NotifierThreadProc: %s",
		"could not make receive pipe close-on-exec");
    }
    if (fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) {
	Tcl_Panic("NotifierThreadProc: %s",
		"could not make trigger pipe close-on-exec");
    }

    /*
     * Install the write end of the pipe into the global variable.
     */

    pthread_mutex_lock(&notifierMutex);
    triggerPipe = fds[1];

    /*
     * Signal any threads that are waiting.
     */

    pthread_cond_broadcast(&notifierCV);
    pthread_mutex_unlock(&notifierMutex);

    /*
     * Look for file events and report them to interested threads.
     */

    while (1) {
	FD_ZERO(&readableMask);
	FD_ZERO(&writableMask);
	FD_ZERO(&exceptionMask);

	/*
	 * Compute the logical OR of the masks from all the waiting
	 * notifiers.
	 */

	pthread_mutex_lock(&notifierMutex);
	timePtr = NULL;
	for (tsdPtr = waitingListPtr; tsdPtr; tsdPtr = tsdPtr->nextPtr) {
	    for (i = tsdPtr->numFdBits-1; i >= 0; --i) {
		if (FD_ISSET(i, &tsdPtr->checkMasks.readable)) {
		    FD_SET(i, &readableMask);
		}
		if (FD_ISSET(i, &tsdPtr->checkMasks.writable)) {
		    FD_SET(i, &writableMask);
		}
		if (FD_ISSET(i, &tsdPtr->checkMasks.exception)) {
		    FD_SET(i, &exceptionMask);
		}
	    }
	    if (tsdPtr->numFdBits > numFdBits) {
		numFdBits = tsdPtr->numFdBits;
	    }
	    if (tsdPtr->pollState & POLL_WANT) {
		/*
		 * Here we make sure we go through select() with the same mask
		 * bits that were present when the thread tried to poll.
		 */

		tsdPtr->pollState |= POLL_DONE;
		timePtr = &poll;
	    }
	}
	pthread_mutex_unlock(&notifierMutex);

	/*
	 * Set up the mask to include the receive pipe.
	 */

	if (receivePipe >= numFdBits) {
	    numFdBits = receivePipe + 1;
	}
	FD_SET(receivePipe, &readableMask);

	if (select(numFdBits, &readableMask, &writableMask, &exceptionMask,
		timePtr) == -1) {
	    /*
	     * Try again immediately on an error.
	     */

	    continue;
	}

	/*
	 * Alert any threads that are waiting on a ready file descriptor.
	 */

	pthread_mutex_lock(&notifierMutex);
	for (tsdPtr = waitingListPtr; tsdPtr; tsdPtr = tsdPtr->nextPtr) {
	    found = 0;

	    for (i = tsdPtr->numFdBits-1; i >= 0; --i) {
		if (FD_ISSET(i, &tsdPtr->checkMasks.readable)
			&& FD_ISSET(i, &readableMask)) {
		    FD_SET(i, &tsdPtr->readyMasks.readable);
		    found = 1;
		}
		if (FD_ISSET(i, &tsdPtr->checkMasks.writable)
			&& FD_ISSET(i, &writableMask)) {
		    FD_SET(i, &tsdPtr->readyMasks.writable);
		    found = 1;
		}
		if (FD_ISSET(i, &tsdPtr->checkMasks.exception)
			&& FD_ISSET(i, &exceptionMask)) {
		    FD_SET(i, &tsdPtr->readyMasks.exception);
		    found = 1;
		}
	    }

	    if (found || (tsdPtr->pollState & POLL_DONE)) {
		AlertSingleThread(tsdPtr);
	    }
	}
	pthread_mutex_unlock(&notifierMutex);

	/*
	 * Consume the next byte from the notifier pipe if the pipe was
	 * readable. Note that there may be multiple bytes pending, but to
	 * avoid a race condition we only read one at a time.
	 */

	do {
	    i = read(receivePipe, buf, 1);
	    if (i <= 0) {
		break;
	    } else if ((i == 0) || ((i == 1) && (buf[0] == 'q'))) {
		/*
		 * Someone closed the write end of the pipe or sent us a Quit
		 * message [Bug: 4139] and then closed the write end of the
		 * pipe so we need to shut down the notifier thread.
		 */

		break;
	    }
	} while (1);
	if ((i == 0) || (buf[0] == 'q')) {
	    break;
	}
    }

    /*
     * Clean up the read end of the pipe and signal any threads waiting on
     * termination of the notifier thread.
     */

    close(receivePipe);
    pthread_mutex_lock(&notifierMutex);
    triggerPipe = -1;
    pthread_cond_broadcast(&notifierCV);
    pthread_mutex_unlock(&notifierMutex);

    TclpThreadExit(0);
}
#endif /* TCL_THREADS */

#endif /* (!NOTIFIER_EPOLL && !NOTIFIER_KQUEUE) || !TCL_THREADS */
#endif /* !HAVE_COREFOUNDATION */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
