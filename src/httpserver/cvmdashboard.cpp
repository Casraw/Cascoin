// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <httpserver/cvmdashboard.h>
#include <httpserver/cvmdashboard_html.h>
#include <httpserver/cvmdashboard_contracts.h>
#include <httpserver/cvmdashboard_evm.h>
#include <httpserver.h>
#include <rpc/protocol.h>
#include <util.h>
#include <utilstrencodings.h>
#include <chainparamsbase.h>

// All HTML/CSS/JS is now embedded in cvmdashboard_html.h and cvmdashboard_evm.h
// No external files needed!

/**
 * Build the complete dashboard HTML by combining base dashboard with EVM extensions
 * 
 * Requirements: 1.4, 1.5, 6.1, 6.3, 18.2, 2.1, 14.1, 22.5
 */
std::string BuildCompleteDashboardHTML() {
    std::string html = CVMDashboardHTML::INDEX_HTML;
    
    // Find the insertion point (before the footer)
    size_t footerPos = html.find("<footer class=\"footer\">");
    if (footerPos != std::string::npos) {
        // Insert Contract Management section before EVM sections
        std::string allSections = CVMDashboardContracts::CONTRACT_MANAGEMENT_SECTION;
        allSections += CVMDashboardEVM::EVM_CONTRACT_SECTION;
        allSections += CVMDashboardEVM::GAS_MANAGEMENT_SECTION;
        allSections += CVMDashboardEVM::TRUST_AWARE_SECTION;
        
        html.insert(footerPos, allSections);
    }
    
    // Find the script section and add Contract Management + EVM JavaScript
    size_t scriptEndPos = html.rfind("</script>");
    if (scriptEndPos != std::string::npos) {
        std::string allJS = CVMDashboardContracts::CONTRACT_MANAGEMENT_JS;
        allJS += CVMDashboardEVM::EVM_DASHBOARD_JS;
        allJS += CVMDashboardEVM::GAS_MANAGEMENT_JS;
        allJS += CVMDashboardEVM::TRUST_AWARE_JS;
        
        html.insert(scriptEndPos, allJS);
    }
    
    return html;
}

// Cache the complete dashboard HTML
static std::string g_completeDashboardHTML;
static bool g_dashboardInitialized = false;

bool CVMDashboardRequestHandler(HTTPRequest* req, const std::string& strReq) {
    LogPrint(BCLog::HTTP, "CVM Dashboard: Request for %s\n", strReq);
    
    // Only allow GET requests
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    // Build complete dashboard HTML on first request (lazy initialization)
    if (!g_dashboardInitialized) {
        g_completeDashboardHTML = BuildCompleteDashboardHTML();
        g_dashboardInitialized = true;
        LogPrint(BCLog::HTTP, "CVM Dashboard: Built complete HTML (%d bytes)\n", 
                 g_completeDashboardHTML.length());
    }
    
    // Serve embedded HTML page (single-page app with EVM extensions)
    req->WriteHeader("Content-Type", "text/html; charset=utf-8");
    req->WriteHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    req->WriteHeader("Pragma", "no-cache");
    req->WriteHeader("Expires", "0");
    req->WriteReply(HTTP_OK, g_completeDashboardHTML);
    
    LogPrint(BCLog::HTTP, "CVM Dashboard: Served complete HTML (%d bytes)\n", 
             g_completeDashboardHTML.length());
    
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
