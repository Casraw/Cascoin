// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <httpserver/cvmdashboard.h>
#include <httpserver/cvmdashboard_html.h>
#include <httpserver.h>
#include <rpc/protocol.h>
#include <util.h>
#include <utilstrencodings.h>
#include <chainparamsbase.h>

// All HTML/CSS/JS is now embedded in cvmdashboard_html.h
// No external files needed!

bool CVMDashboardRequestHandler(HTTPRequest* req, const std::string& strReq) {
    LogPrint(BCLog::HTTP, "CVM Dashboard: Request for %s\n", strReq);
    
    // Only allow GET requests
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    // Serve embedded HTML page (single-page app)
    req->WriteHeader("Content-Type", "text/html; charset=utf-8");
    req->WriteHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    req->WriteHeader("Pragma", "no-cache");
    req->WriteHeader("Expires", "0");
    req->WriteReply(HTTP_OK, CVMDashboardHTML::INDEX_HTML);
    
    LogPrint(BCLog::HTTP, "CVM Dashboard: Served embedded HTML (%d bytes)\n", 
             CVMDashboardHTML::INDEX_HTML.length());
    
    return true;
}

void InitCVMDashboardHandlers() {
    LogPrintf("Initializing CVM Dashboard handlers...\n");
    
    // Register handler for /dashboard/* paths
    RegisterHTTPHandler("/dashboard", false, CVMDashboardRequestHandler);
    
    // Also register root path for convenience
    RegisterHTTPHandler("/cvm", false, CVMDashboardRequestHandler);
    
    // Use BaseParams() instead of Params() for safety during early initialization
    try {
        LogPrintf("CVM Dashboard available at http://localhost:%d/dashboard/\n", 
                  gArgs.GetArg("-rpcport", BaseParams().RPCPort()));
    } catch (const std::exception& e) {
        LogPrintf("CVM Dashboard: Could not get RPC port (chain params not initialized yet): %s\n", e.what());
    }
}
