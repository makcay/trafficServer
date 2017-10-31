/* blacklist-1.c:  An example program that denies client access
 *                 to blacklisted sites. This plugin illustrates
 *                 how to use configuration information from the
 *                 blacklist.txt configuration file.
 *
 * Usage:
 * (Solaris) : blacklist-1.so
 *
 *
 */

#include <stdio.h>
#include <string.h>
#include <ts/ts.h>

#define MAX_NSITES 500

static char* sites[MAX_NSITES];
static int nsites;
static TSMutex sites_mutex;
static TSTextLogObject log;

static void
handle_dns (TSHttpTxn txnp, TSCont contp)
{
    TSMBuffer bufp;
    TSMLoc hdr_loc;
    TSMLoc url_loc;
    const char *host;
    int i;
    int host_length;

    if (!TSHttpTxnClientReqGet (txnp, &bufp, &hdr_loc)) {
        TSError ("couldn't retrieve client request header\n");
        goto done;
    }

    TSHttpHdrUrlGet (bufp, hdr_loc,&url_loc);
    if (!url_loc) {
        TSError ("couldn't retrieve request url\n");
        TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
        goto done;
    }

    host = TSUrlHostGet (bufp, url_loc, &host_length);
    if (!host) {
        TSError ("couldn't retrieve request hostname\n");
        TSHandleMLocRelease (bufp, hdr_loc, url_loc);
        TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
        goto done;
    }

    TSMutexLock(sites_mutex);

    for (i = 0; i < nsites; i++) {
        if (strncmp (host, sites[i], host_length) == 0) {
            if (log) {
                TSTextLogObjectWrite(log, "blacklisting site: %s", sites[i]);
            } else {
                printf ("blacklisting site: %s\n", sites[i]);
            }
            TSHttpTxnHookAdd (txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
            TSHandleMLocRelease (bufp, hdr_loc, url_loc);
            TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
            TSHttpTxnReenable (txnp, TS_EVENT_HTTP_ERROR);
            TSMutexUnlock(sites_mutex);
            return;
        }
    }

    TSMutexUnlock(sites_mutex);
    TSHandleMLocRelease (bufp, hdr_loc, url_loc);
    TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);

 done:
    TSHttpTxnReenable (txnp, TS_EVENT_HTTP_CONTINUE);
}

static void
handle_response (TSHttpTxn txnp)
{
    TSMBuffer bufp;
    TSMLoc hdr_loc;
    TSMLoc url_loc;
    char *url_str;
    char *buf;
    int url_length;

    if (!TSHttpTxnClientRespGet (txnp, &bufp, &hdr_loc)) {
        TSError ("couldn't retrieve client response header\n");
        goto done;
    }

    TSHttpHdrStatusSet (bufp, hdr_loc, TS_HTTP_STATUS_FORBIDDEN);
    TSHttpHdrReasonSet (bufp, hdr_loc,
        TSHttpHdrReasonLookup (TS_HTTP_STATUS_FORBIDDEN),
        strlen (TSHttpHdrReasonLookup (TS_HTTP_STATUS_FORBIDDEN)) );

    if (!TSHttpTxnClientReqGet (txnp, &bufp, &hdr_loc)) {
        TSError ("couldn't retrieve client request header\n");
        TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
        goto done;
    }

    //url_loc = TSHttpHdrUrlGet (bufp, hdr_loc);
    TSHttpHdrUrlGet (bufp, hdr_loc, &url_loc);
    if (!url_loc) {
        TSError ("couldn't retrieve request url\n");
        TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
        goto done;
    }

    buf = (char *)TSmalloc (4096);

    url_str = TSUrlStringGet (bufp, url_loc, &url_length);
    sprintf (buf, "You are forbidden from accessing \"%s\"\n", url_str);
    TSfree (url_str);
    TSHandleMLocRelease (bufp, hdr_loc, url_loc);
    TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);

    TSHttpTxnErrorBodySet (txnp, buf, strlen (buf), NULL);

 done:
    TSHttpTxnReenable (txnp, TS_EVENT_HTTP_CONTINUE);
}

static void
read_blacklist (void)
{
    char blacklist_file[1024];
    TSFile file;

    sprintf (blacklist_file, "%s/blacklist.txt", TSPluginDirGet());
    file = TSfopen(blacklist_file, "r");

    TSMutexLock (sites_mutex);
    nsites = 0;

    if (file != NULL) {
        char buffer[1024];

        while (TSfgets (file, buffer, sizeof(buffer)-1) != NULL && nsites < MAX_NSITES) {
            char* eol;
            if ((eol = strstr(buffer, "\r\n")) != NULL) {
                /* To handle newlines on Windows */
                *eol = '\0';
            } else if ((eol = strchr(buffer, '\n')) != NULL) {
                *eol = '\0';
            } else {
                /* Not a valid line, skip it */
                continue;
           }
           if (sites[nsites] != NULL) {
                TSfree (sites[nsites]);
           }
           sites[nsites] = TSstrdup (buffer);
           nsites++;
       }

        TSfclose (file);
    } else {
       TSError ("unable to open %s\n", blacklist_file);
       TSError ("all sites will be allowed %s\n", blacklist_file);
    }

    TSMutexUnlock (sites_mutex);
}

static int
blacklist_plugin (TSCont contp, TSEvent event, void *edata)
{
    TSHttpTxn txnp = (TSHttpTxn) edata;

    switch (event) {
    case TS_EVENT_HTTP_OS_DNS:
        handle_dns (txnp, contp);
        return 0;
    case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
        handle_response (txnp);
        return 0;
    case TS_EVENT_MGMT_UPDATE:
        read_blacklist ();
        return 0;
    default:
        break;
    }
    return 0;
}

int
check_ts_version()
{

   const char *ts_version = TSTrafficServerVersionGet();
   int result = 0;

   if (ts_version) {
       int major_ts_version = 0;
       int minor_ts_version = 0;
       int patch_ts_version = 0;

       if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
            return 0;
       }

       /* Need at least TS 2.0 */
       if (major_ts_version >= 2) {
            result = 1;
       }

   }

   return result;
}

void
TSPluginInit (int argc, const char *argv[])
{
    int i;
    TSCont contp;
    TSPluginRegistrationInfo info;

    info.plugin_name = "blacklist-1";
    info.vendor_name = "DsCompany";
    info.support_email = "ts-api-support@DsCompany.com";

    if (!TSPluginRegister (TS_SDK_VERSION_2_0 , &info)) {
        TSError ("Plugin registration failed.\n");
    }

    if (!check_ts_version()) {
       TSError ("Plugin requires Traffic Server 2.0 or later\n");
       return;
    }

    /* create an TSTextLogObject to log blacklisted requests to */
    TSReturnCode error = TSTextLogObjectCreate("blacklist", TS_LOG_MODE_ADD_TIMESTAMP,
             &log);
    if (error != TS_SUCCESS) {
        printf("Blacklist plugin: error %d while creating log\n", error);
    }

    sites_mutex = TSMutexCreate ();

    nsites = 0;
    for (i = 0; i < MAX_NSITES; i++) {
        sites[i] = NULL;
    }

    read_blacklist ();

    contp = TSContCreate (blacklist_plugin, NULL);

    TSHttpHookAdd (TS_HTTP_OS_DNS_HOOK, contp);

    //TSMgmtUpdateRegister (contp, "Super Blacklist Plugin", "blacklist.cgi");
    TSMgmtUpdateRegister (contp, "Super Blacklist Plugin");
}
