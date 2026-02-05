/**
 * @file event_loop.c
 * @brief Background GLib main loop implementation
 */

#include "event_loop.h"
#include <stdio.h>
#include <pthread.h>

/* Global state */
static pthread_mutex_t g_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_loop_thread = 0;
static GMainLoop *g_main_loop = NULL;
static GMainContext *g_main_context = NULL;
static bool g_loop_running = false;

/**
 * Event loop thread function
 */
static void* event_loop_thread_func(void *arg)
{
    (void)arg;
    
    fprintf(stderr, "[librdkFwupdateMgr] Event loop thread started (tid=%lu)\n",
            (unsigned long)pthread_self());
    
    /* Run the main loop (blocks until g_main_loop_quit()) */
    g_main_loop_run(g_main_loop);
    
    fprintf(stderr, "[librdkFwupdateMgr] Event loop thread exiting\n");
    return NULL;
}

/**
 * Start the event loop thread
 */
bool event_loop_start(void)
{
    pthread_mutex_lock(&g_loop_mutex);
    
    /* Already running? */
    if (g_loop_running) {
        pthread_mutex_unlock(&g_loop_mutex);
        fprintf(stderr, "[librdkFwupdateMgr] Event loop already running\n");
        return true;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] Starting event loop...\n");
    
    /* Create dedicated main context for this thread */
    g_main_context = g_main_context_new();
    if (!g_main_context) {
        fprintf(stderr, "[librdkFwupdateMgr] Failed to create GMainContext\n");
        pthread_mutex_unlock(&g_loop_mutex);
        return false;
    }
    
    /* Create main loop attached to our context */
    g_main_loop = g_main_loop_new(g_main_context, FALSE);
    if (!g_main_loop) {
        fprintf(stderr, "[librdkFwupdateMgr] Failed to create GMainLoop\n");
        g_main_context_unref(g_main_context);
        g_main_context = NULL;
        pthread_mutex_unlock(&g_loop_mutex);
        return false;
    }
    
    /* Create background thread */
    int ret = pthread_create(&g_loop_thread, NULL, event_loop_thread_func, NULL);
    if (ret != 0) {
        fprintf(stderr, "[librdkFwupdateMgr] Failed to create event loop thread: %d\n", ret);
        g_main_loop_unref(g_main_loop);
        g_main_loop = NULL;
        g_main_context_unref(g_main_context);
        g_main_context = NULL;
        pthread_mutex_unlock(&g_loop_mutex);
        return false;
    }
    
    g_loop_running = true;
    pthread_mutex_unlock(&g_loop_mutex);
    
    fprintf(stderr, "[librdkFwupdateMgr] Event loop started successfully\n");
    return true;
}

/**
 * Stop the event loop thread
 */
void event_loop_stop(void)
{
    pthread_mutex_lock(&g_loop_mutex);
    
    if (!g_loop_running) {
        pthread_mutex_unlock(&g_loop_mutex);
        return;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] Stopping event loop...\n");
    
    /* Signal the main loop to quit */
    if (g_main_loop) {
        g_main_loop_quit(g_main_loop);
    }
    
    pthread_mutex_unlock(&g_loop_mutex);
    
    /* Wait for thread to exit (outside lock to avoid deadlock) */
    if (g_loop_thread) {
        fprintf(stderr, "[librdkFwupdateMgr] Waiting for event loop thread to exit...\n");
        pthread_join(g_loop_thread, NULL);
        g_loop_thread = 0;
    }
    
    pthread_mutex_lock(&g_loop_mutex);
    
    /* Cleanup */
    if (g_main_loop) {
        g_main_loop_unref(g_main_loop);
        g_main_loop = NULL;
    }
    
    if (g_main_context) {
        g_main_context_unref(g_main_context);
        g_main_context = NULL;
    }
    
    g_loop_running = false;
    pthread_mutex_unlock(&g_loop_mutex);
    
    fprintf(stderr, "[librdkFwupdateMgr] Event loop stopped\n");
}

/**
 * Check if event loop is running
 */
bool event_loop_is_running(void)
{
    pthread_mutex_lock(&g_loop_mutex);
    bool running = g_loop_running;
    pthread_mutex_unlock(&g_loop_mutex);
    return running;
}

/**
 * Get the event loop's GMainContext
 */
GMainContext* event_loop_get_context(void)
{
    pthread_mutex_lock(&g_loop_mutex);
    GMainContext *ctx = g_main_context;
    pthread_mutex_unlock(&g_loop_mutex);
    return ctx;
}
