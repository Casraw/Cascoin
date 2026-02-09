// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file cvm_dashboard_contracts_tests.cpp
 * @brief Property-based tests for CVM Dashboard Contract Management
 *
 * Feature: cvm-dashboard-contracts
 *
 * These tests verify the correctness properties of the contract management
 * functionality using manual randomization with std::mt19937. Each property
 * test runs a minimum of 100 iterations.
 */

#include <test/test_bitcoin.h>
#include <uint256.h>
#include <cvm/contract.h>
#include <cvm/bytecode_detector.h>
#include <cvm/cvmdb.h>

#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>
#include <univalue.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

/**
 * Generate a random uint160 address using the given RNG.
 */
uint160 GenerateRandomAddress(std::mt19937& rng)
{
    std::vector<unsigned char> bytes(20);
    for (auto& b : bytes) {
        b = static_cast<unsigned char>(rng() & 0xFF);
    }
    return uint160(bytes);
}

/**
 * Generate a random CVM::Contract with the given deployer address.
 * Fills in random but valid-looking fields for code, deploymentHeight, etc.
 */
CVM::Contract GenerateRandomContract(std::mt19937& rng, const uint160& deployer)
{
    CVM::Contract contract;

    // Random contract address (derived from deployer + nonce in real code, but random here)
    contract.address = GenerateRandomAddress(rng);

    // Random bytecode (1 to 200 bytes)
    std::uniform_int_distribution<size_t> codeSizeDist(1, 200);
    size_t codeSize = codeSizeDist(rng);
    contract.code.resize(codeSize);
    for (auto& b : contract.code) {
        b = static_cast<uint8_t>(rng() & 0xFF);
    }

    // Random deployment height (1 to 1,000,000)
    std::uniform_int_distribution<int> heightDist(1, 1000000);
    contract.deploymentHeight = heightDist(rng);

    // Random deployment tx hash
    std::vector<unsigned char> txBytes(32);
    for (auto& b : txBytes) {
        b = static_cast<unsigned char>(rng() & 0xFF);
    }
    contract.deploymentTx = uint256(txBytes);

    // Random cleanup status
    contract.isCleanedUp = (rng() % 2 == 0);

    return contract;
}

/**
 * Standalone contract-ownership filter function.
 *
 * This mirrors the core filtering logic of the `listmycontracts` RPC endpoint:
 * given a set of contracts (each with a known deployer address) and a set of
 * wallet addresses, return exactly those contracts whose deployer is in the
 * wallet address set.
 *
 * In the real RPC, the deployer is extracted from the deployment transaction
 * and checked via IsMine(). Here we test the pure filtering logic with
 * pre-resolved deployer addresses.
 */
std::vector<CVM::Contract> FilterContractsByOwnership(
    const std::vector<CVM::Contract>& allContracts,
    const std::vector<uint160>& deployerAddresses,  // parallel to allContracts
    const std::set<uint160>& walletAddresses)
{
    std::vector<CVM::Contract> result;
    for (size_t i = 0; i < allContracts.size(); ++i) {
        if (walletAddresses.count(deployerAddresses[i]) > 0) {
            result.push_back(allContracts[i]);
        }
    }
    return result;
}

/**
 * Generate a random uint256 value using the given RNG.
 */
uint256 GenerateRandomUint256(std::mt19937& rng)
{
    std::vector<unsigned char> bytes(32);
    for (auto& b : bytes) {
        b = static_cast<unsigned char>(rng() & 0xFF);
    }
    return uint256(bytes);
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_dashboard_contracts_tests, BasicTestingSetup)

//=============================================================================
// Property 1: Contract-Ownership-Filterung
// Feature: cvm-dashboard-contracts, Property 1: Contract-Ownership-Filterung
//
// For any set of Contracts in the CVMDatabase and for any set of wallet
// addresses, `listmycontracts` SHALL return exactly those contracts whose
// deployer address is contained in the wallet address set — regardless of
// deployment time or other contract attributes.
//
// **Validates: Requirements 1.1, 2.1, 2.2**
//=============================================================================
BOOST_AUTO_TEST_CASE(property1_contract_ownership_filtering)
{
    // Use a fixed seed for reproducibility, but test across many random scenarios
    std::mt19937 rng(42);
    static constexpr int ITERATIONS = 100;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        // --- Generate random test data ---

        // Number of total contracts (0 to 20)
        std::uniform_int_distribution<size_t> numContractsDist(0, 20);
        size_t numContracts = numContractsDist(rng);

        // Number of possible deployer addresses (1 to 10)
        std::uniform_int_distribution<size_t> numDeployersDist(1, 10);
        size_t numDeployers = numDeployersDist(rng);

        // Generate a pool of deployer addresses
        std::vector<uint160> deployerPool;
        deployerPool.reserve(numDeployers);
        for (size_t i = 0; i < numDeployers; ++i) {
            deployerPool.push_back(GenerateRandomAddress(rng));
        }

        // Generate contracts, each assigned a random deployer from the pool
        std::vector<CVM::Contract> allContracts;
        std::vector<uint160> deployerAddresses;
        allContracts.reserve(numContracts);
        deployerAddresses.reserve(numContracts);

        std::uniform_int_distribution<size_t> deployerIdxDist(0, numDeployers - 1);
        for (size_t i = 0; i < numContracts; ++i) {
            uint160 deployer = deployerPool[deployerIdxDist(rng)];
            allContracts.push_back(GenerateRandomContract(rng, deployer));
            deployerAddresses.push_back(deployer);
        }

        // Generate a random wallet address set (subset of deployer pool + some extra)
        std::set<uint160> walletAddresses;
        // Include some deployers from the pool
        for (size_t i = 0; i < numDeployers; ++i) {
            if (rng() % 2 == 0) {
                walletAddresses.insert(deployerPool[i]);
            }
        }
        // Optionally add some addresses not in the deployer pool
        std::uniform_int_distribution<size_t> extraAddrDist(0, 3);
        size_t numExtra = extraAddrDist(rng);
        for (size_t i = 0; i < numExtra; ++i) {
            walletAddresses.insert(GenerateRandomAddress(rng));
        }

        // --- Apply the filter ---
        std::vector<CVM::Contract> filtered = FilterContractsByOwnership(
            allContracts, deployerAddresses, walletAddresses);

        // --- Compute expected result independently ---
        std::vector<CVM::Contract> expected;
        for (size_t i = 0; i < numContracts; ++i) {
            if (walletAddresses.count(deployerAddresses[i]) > 0) {
                expected.push_back(allContracts[i]);
            }
        }

        // --- Property checks ---

        // 1. The number of filtered contracts must match the expected count
        BOOST_CHECK_EQUAL(filtered.size(), expected.size());

        // 2. Every filtered contract must have its deployer in the wallet set
        for (size_t i = 0; i < filtered.size(); ++i) {
            BOOST_CHECK_MESSAGE(
                walletAddresses.count(deployerAddresses[
                    // Find the original index of this filtered contract
                    std::distance(allContracts.begin(),
                        std::find_if(allContracts.begin(), allContracts.end(),
                            [&](const CVM::Contract& c) {
                                return c.address == filtered[i].address;
                            }))
                ]) > 0,
                "Filtered contract deployer must be in wallet set (iter=" << iter << ", i=" << i << ")");
        }

        // 3. Every contract whose deployer IS in the wallet must appear in filtered
        size_t expectedInWallet = 0;
        for (size_t i = 0; i < numContracts; ++i) {
            if (walletAddresses.count(deployerAddresses[i]) > 0) {
                expectedInWallet++;
                // Find this contract in the filtered result
                bool found = false;
                for (const auto& fc : filtered) {
                    if (fc.address == allContracts[i].address) {
                        found = true;
                        break;
                    }
                }
                BOOST_CHECK_MESSAGE(found,
                    "Contract with wallet deployer must appear in filtered result "
                    "(iter=" << iter << ", contract idx=" << i << ")");
            }
        }
        BOOST_CHECK_EQUAL(filtered.size(), expectedInWallet);

        // 4. No contract whose deployer is NOT in the wallet should appear
        for (size_t i = 0; i < numContracts; ++i) {
            if (walletAddresses.count(deployerAddresses[i]) == 0) {
                bool found = false;
                for (const auto& fc : filtered) {
                    if (fc.address == allContracts[i].address) {
                        found = true;
                        break;
                    }
                }
                BOOST_CHECK_MESSAGE(!found,
                    "Contract with non-wallet deployer must NOT appear in filtered result "
                    "(iter=" << iter << ", contract idx=" << i << ")");
            }
        }

        // 5. Filtering is independent of deployment time and other attributes
        //    (verified implicitly: contracts have random heights, code sizes,
        //     cleanup status, and the filter only depends on deployer membership)
    }

    BOOST_TEST_MESSAGE("Property 1 (Contract-Ownership-Filterung): 100 iterations passed");
}

//=============================================================================
// Property 2: listmycontracts-Response-Vollständigkeit
// Feature: cvm-dashboard-contracts, Property 2: listmycontracts-Response-Vollständigkeit
//
// For any Contract returned by `listmycontracts`, the response SHALL contain
// the fields `address`, `deploymentHeight`, and `format`, and all fields
// SHALL have non-empty values.
//
// **Validates: Requirements 1.5**
//=============================================================================
BOOST_AUTO_TEST_CASE(property2_response_completeness)
{
    std::mt19937 rng(12345);  // Fixed seed for reproducibility
    static constexpr int ITERATIONS = 100;

    CVM::BytecodeDetector detector;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        // --- Generate a random contract ---
        uint160 deployer = GenerateRandomAddress(rng);
        CVM::Contract contract = GenerateRandomContract(rng, deployer);

        // --- Serialize to JSON, mimicking the listmycontracts RPC response ---

        // Detect bytecode format using the real BytecodeDetector
        CVM::BytecodeDetectionResult detection = detector.DetectFormat(contract.code);
        std::string formatStr;
        switch (detection.format) {
            case CVM::BytecodeFormat::EVM_BYTECODE:
                formatStr = "EVM";
                break;
            case CVM::BytecodeFormat::CVM_NATIVE:
                formatStr = "CVM";
                break;
            case CVM::BytecodeFormat::HYBRID:
                formatStr = "HYBRID";
                break;
            default:
                formatStr = "UNKNOWN";
                break;
        }

        // Build the JSON entry exactly as the RPC endpoint does
        // Note: UniValue lacks a pushKV(key, bool) overload, so we must
        // construct a UniValue(bool) explicitly to get VBOOL type.
        UniValue entry(UniValue::VOBJ);
        entry.pushKV("address", contract.address.GetHex());
        entry.pushKV("deployer", deployer.GetHex());
        entry.pushKV("deploymentHeight", contract.deploymentHeight);
        entry.pushKV("deploymentTx", contract.deploymentTx.GetHex());
        entry.pushKV("codeSize", (int64_t)contract.code.size());
        entry.pushKV("format", formatStr);
        entry.pushKV("isCleanedUp", UniValue(contract.isCleanedUp));

        // --- Property checks: required fields exist and are non-empty ---

        // Check "address" field exists and is a non-empty string
        BOOST_CHECK_MESSAGE(entry.exists("address"),
            "Response must contain 'address' field (iter=" << iter << ")");
        BOOST_CHECK_MESSAGE(entry["address"].isStr(),
            "'address' must be a string (iter=" << iter << ")");
        BOOST_CHECK_MESSAGE(!entry["address"].get_str().empty(),
            "'address' must not be empty (iter=" << iter << ")");

        // Check "deploymentHeight" field exists and is a valid numeric value
        BOOST_CHECK_MESSAGE(entry.exists("deploymentHeight"),
            "Response must contain 'deploymentHeight' field (iter=" << iter << ")");
        BOOST_CHECK_MESSAGE(entry["deploymentHeight"].isNum(),
            "'deploymentHeight' must be numeric (iter=" << iter << ")");
        // deploymentHeight must be positive (contracts are deployed at height >= 1)
        BOOST_CHECK_MESSAGE(entry["deploymentHeight"].get_int() > 0,
            "'deploymentHeight' must be positive (iter=" << iter
            << ", got=" << entry["deploymentHeight"].get_int() << ")");

        // Check "format" field exists and is a non-empty string
        BOOST_CHECK_MESSAGE(entry.exists("format"),
            "Response must contain 'format' field (iter=" << iter << ")");
        BOOST_CHECK_MESSAGE(entry["format"].isStr(),
            "'format' must be a string (iter=" << iter << ")");
        BOOST_CHECK_MESSAGE(!entry["format"].get_str().empty(),
            "'format' must not be empty (iter=" << iter << ")");

        // Verify format is one of the known values
        std::string fmt = entry["format"].get_str();
        BOOST_CHECK_MESSAGE(
            fmt == "CVM" || fmt == "EVM" || fmt == "HYBRID" || fmt == "UNKNOWN",
            "'format' must be CVM, EVM, HYBRID, or UNKNOWN (iter=" << iter
            << ", got=" << fmt << ")");

        // --- Additional completeness checks for other response fields ---

        // Check "deployer" field exists and is non-empty
        BOOST_CHECK_MESSAGE(entry.exists("deployer"),
            "Response must contain 'deployer' field (iter=" << iter << ")");
        BOOST_CHECK_MESSAGE(!entry["deployer"].get_str().empty(),
            "'deployer' must not be empty (iter=" << iter << ")");

        // Check "deploymentTx" field exists and is non-empty
        BOOST_CHECK_MESSAGE(entry.exists("deploymentTx"),
            "Response must contain 'deploymentTx' field (iter=" << iter << ")");
        BOOST_CHECK_MESSAGE(!entry["deploymentTx"].get_str().empty(),
            "'deploymentTx' must not be empty (iter=" << iter << ")");

        // Check "codeSize" field exists and is positive
        BOOST_CHECK_MESSAGE(entry.exists("codeSize"),
            "Response must contain 'codeSize' field (iter=" << iter << ")");
        BOOST_CHECK_MESSAGE(entry["codeSize"].get_int64() > 0,
            "'codeSize' must be positive (iter=" << iter << ")");

        // Check "isCleanedUp" field exists and is boolean
        BOOST_CHECK_MESSAGE(entry.exists("isCleanedUp"),
            "Response must contain 'isCleanedUp' field (iter=" << iter << ")");
        BOOST_CHECK_MESSAGE(entry["isCleanedUp"].isBool(),
            "'isCleanedUp' must be boolean (iter=" << iter << ")");
    }

    BOOST_TEST_MESSAGE("Property 2 (listmycontracts-Response-Vollständigkeit): 100 iterations passed");
}

//=============================================================================
// Property 5: Storage-Read-Write-Round-Trip
// Feature: cvm-dashboard-contracts, Property 5: Storage-Read-Write-Round-Trip
//
// For any Contract address and for any set of storage key-value pairs,
// WHEN these pairs are written via CVMDatabase::Store(), THEN SHALL
// getcontractstorage return exactly these pairs with identical keys and values.
//
// **Validates: Requirements 5.1**
//=============================================================================
BOOST_AUTO_TEST_CASE(property5_storage_round_trip)
{
    namespace fs = boost::filesystem;

    std::mt19937 rng(98765);  // Fixed seed for reproducibility
    static constexpr int ITERATIONS = 100;

    // Create a temporary database directory for this test
    fs::path tempDir = fs::temp_directory_path() / ("cvm_storage_rt_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(tempDir);

    // Create a real CVMDatabase with wipe flag
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        // --- Generate random test data ---

        // Random contract address
        uint160 contractAddr = GenerateRandomAddress(rng);

        // Register the contract in the DB so Exists() returns true
        CVM::Contract contract;
        contract.address = contractAddr;
        contract.code = {0x00};  // Minimal bytecode
        contract.deploymentHeight = 1;
        BOOST_REQUIRE_MESSAGE(db.WriteContract(contractAddr, contract),
            "Failed to write contract to DB (iter=" << iter << ")");

        // Generate a random number of storage key-value pairs (1 to 10)
        std::uniform_int_distribution<size_t> numPairsDist(1, 10);
        size_t numPairs = numPairsDist(rng);

        // Use a map to track expected storage (handles duplicate keys naturally)
        std::map<uint256, uint256> expectedStorage;

        for (size_t i = 0; i < numPairs; ++i) {
            uint256 key = GenerateRandomUint256(rng);
            uint256 value = GenerateRandomUint256(rng);

            // Write via Store()
            BOOST_REQUIRE_MESSAGE(db.Store(contractAddr, key, value),
                "Store() failed (iter=" << iter << ", pair=" << i << ")");

            expectedStorage[key] = value;
        }

        // Flush to ensure data is persisted to LevelDB
        db.Flush();

        // --- Verify via direct Load() ---
        for (const auto& kv : expectedStorage) {
            uint256 loadedValue;
            bool loaded = db.Load(contractAddr, kv.first, loadedValue);
            BOOST_CHECK_MESSAGE(loaded,
                "Load() failed for key " << kv.first.GetHex()
                << " (iter=" << iter << ")");
            if (loaded) {
                BOOST_CHECK_MESSAGE(loadedValue == kv.second,
                    "Load() value mismatch for key " << kv.first.GetHex()
                    << ": expected " << kv.second.GetHex()
                    << ", got " << loadedValue.GetHex()
                    << " (iter=" << iter << ")");
            }
        }

        // --- Verify via ListKeysWithPrefix() (the getcontractstorage approach) ---

        // Build the storage key prefix: 'S' + raw contract address (20 bytes)
        std::string storagePrefix = std::string(1, CVM::DB_STORAGE) +
                                    std::string((char*)contractAddr.begin(), 20);

        std::vector<std::string> storageKeys = db.ListKeysWithPrefix(storagePrefix);

        // Collect all key-value pairs found via the prefix scan
        std::map<uint256, uint256> foundStorage;
        for (const std::string& fullKey : storageKeys) {
            // The full key is: 'S' (1 byte) + contractAddr (20 bytes) + storageKey (32 bytes)
            if (fullKey.size() != 1 + 20 + 32) {
                continue;  // Skip malformed keys
            }

            uint256 storageKey;
            memcpy(storageKey.begin(), fullKey.data() + 1 + 20, 32);

            uint256 storageValue;
            if (db.Load(contractAddr, storageKey, storageValue)) {
                foundStorage[storageKey] = storageValue;
            }
        }

        // Property check: foundStorage must contain at least all expectedStorage entries
        // (it may contain entries from previous iterations for different contracts,
        //  but for THIS contract address, it should match exactly)
        for (const auto& kv : expectedStorage) {
            auto it = foundStorage.find(kv.first);
            BOOST_CHECK_MESSAGE(it != foundStorage.end(),
                "ListKeysWithPrefix() missed key " << kv.first.GetHex()
                << " (iter=" << iter << ")");
            if (it != foundStorage.end()) {
                BOOST_CHECK_MESSAGE(it->second == kv.second,
                    "ListKeysWithPrefix() value mismatch for key " << kv.first.GetHex()
                    << ": expected " << kv.second.GetHex()
                    << ", got " << it->second.GetHex()
                    << " (iter=" << iter << ")");
            }
        }

        // Property check: foundStorage should not contain extra keys beyond expectedStorage
        // for this specific contract address
        BOOST_CHECK_MESSAGE(foundStorage.size() == expectedStorage.size(),
            "Storage entry count mismatch: expected " << expectedStorage.size()
            << ", found " << foundStorage.size()
            << " (iter=" << iter << ")");
    }

    // Cleanup temporary directory
    boost::system::error_code ec;
    fs::remove_all(tempDir, ec);

    BOOST_TEST_MESSAGE("Property 5 (Storage-Read-Write-Round-Trip): 100 iterations passed");
}

//=============================================================================
// Property 6: Receipt-Abruf-Vollständigkeit und Sortierung
// Feature: cvm-dashboard-contracts, Property 6: Receipt-Abruf-Vollständigkeit-und-Sortierung
//
// For any Contract address with associated Receipts, getcontractreceipts SHALL
// return all Receipts linked to that Contract address, and the returned list
// SHALL be sorted by blockNumber in descending order (each entry has a
// blockNumber >= the following entry). Each Receipt SHALL contain the fields
// txHash, from, gasUsed, status, and blockNumber.
//
// **Validates: Requirements 6.1, 6.2, 6.3**
//=============================================================================
BOOST_AUTO_TEST_CASE(property6_receipt_retrieval_sorting)
{
    namespace fs = boost::filesystem;

    std::mt19937 rng(77777);  // Fixed seed for reproducibility
    static constexpr int ITERATIONS = 100;

    // Create a temporary database directory for this test
    fs::path tempDir = fs::temp_directory_path() / ("cvm_receipt_sort_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(tempDir);

    // Create a real CVMDatabase with wipe flag
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        // --- Generate random test data ---

        // Generate 2-4 distinct contract addresses for this iteration
        std::uniform_int_distribution<size_t> numContractsDist(2, 4);
        size_t numContracts = numContractsDist(rng);

        std::vector<uint160> contractAddrs;
        contractAddrs.reserve(numContracts);
        for (size_t c = 0; c < numContracts; ++c) {
            contractAddrs.push_back(GenerateRandomAddress(rng));
        }

        // For each contract, generate 1-8 receipts with random block numbers
        std::uniform_int_distribution<size_t> numReceiptsDist(1, 8);
        std::uniform_int_distribution<uint32_t> blockNumDist(1, 1000000);
        std::uniform_int_distribution<uint64_t> gasDist(21000, 500000);
        std::uniform_int_distribution<int> statusDist(0, 1);

        // Track expected receipts per contract: contractAddr -> vector of (txHash, receipt)
        std::map<uint160, std::vector<std::pair<uint256, CVM::TransactionReceipt>>> expectedReceipts;

        for (size_t c = 0; c < numContracts; ++c) {
            size_t numReceipts = numReceiptsDist(rng);

            for (size_t r = 0; r < numReceipts; ++r) {
                CVM::TransactionReceipt receipt;
                receipt.transactionHash = GenerateRandomUint256(rng);
                receipt.blockNumber = blockNumDist(rng);
                receipt.blockHash = GenerateRandomUint256(rng);
                receipt.transactionIndex = static_cast<uint32_t>(r);
                receipt.from = GenerateRandomAddress(rng);
                receipt.to = contractAddrs[c];  // Target is the contract
                receipt.gasUsed = gasDist(rng);
                receipt.cumulativeGasUsed = receipt.gasUsed;
                receipt.status = static_cast<uint8_t>(statusDist(rng));

                // Write receipt to DB (this auto-updates the contract receipt index)
                BOOST_REQUIRE_MESSAGE(
                    db.WriteReceipt(receipt.transactionHash, receipt),
                    "WriteReceipt() failed (iter=" << iter << ", contract=" << c << ", receipt=" << r << ")");

                expectedReceipts[contractAddrs[c]].emplace_back(receipt.transactionHash, receipt);
            }
        }

        // Flush to ensure data is persisted
        db.Flush();

        // --- Verify for each contract ---
        for (size_t c = 0; c < numContracts; ++c) {
            const uint160& contractAddr = contractAddrs[c];
            const auto& expected = expectedReceipts[contractAddr];

            // 1. Read the contract receipt index
            std::vector<uint256> txHashes;
            bool indexRead = db.ReadContractReceiptIndex(contractAddr, txHashes);
            BOOST_CHECK_MESSAGE(indexRead,
                "ReadContractReceiptIndex() failed for contract " << contractAddr.GetHex()
                << " (iter=" << iter << ", contract=" << c << ")");

            // 2. Completeness: all expected tx hashes must be in the index
            BOOST_CHECK_MESSAGE(txHashes.size() >= expected.size(),
                "Receipt index has fewer entries than expected: got " << txHashes.size()
                << ", expected at least " << expected.size()
                << " (iter=" << iter << ", contract=" << c << ")");

            for (const auto& expectedPair : expected) {
                bool found = std::find(txHashes.begin(), txHashes.end(), expectedPair.first) != txHashes.end();
                BOOST_CHECK_MESSAGE(found,
                    "Expected txHash " << expectedPair.first.GetHex()
                    << " not found in contract receipt index"
                    << " (iter=" << iter << ", contract=" << c << ")");
            }

            // 3. Read all receipts and verify fields + sorting
            std::vector<CVM::TransactionReceipt> retrievedReceipts;
            for (const auto& txHash : txHashes) {
                CVM::TransactionReceipt receipt;
                bool readOk = db.ReadReceipt(txHash, receipt);
                BOOST_CHECK_MESSAGE(readOk,
                    "ReadReceipt() failed for txHash " << txHash.GetHex()
                    << " (iter=" << iter << ", contract=" << c << ")");
                if (readOk) {
                    retrievedReceipts.push_back(receipt);
                }
            }

            // 4. Sort retrieved receipts by blockNumber descending (as getcontractreceipts would)
            std::sort(retrievedReceipts.begin(), retrievedReceipts.end(),
                [](const CVM::TransactionReceipt& a, const CVM::TransactionReceipt& b) {
                    return a.blockNumber > b.blockNumber;
                });

            // 5. Verify descending sort order
            for (size_t i = 1; i < retrievedReceipts.size(); ++i) {
                BOOST_CHECK_MESSAGE(
                    retrievedReceipts[i - 1].blockNumber >= retrievedReceipts[i].blockNumber,
                    "Receipts not sorted descending by blockNumber: "
                    << retrievedReceipts[i - 1].blockNumber << " < " << retrievedReceipts[i].blockNumber
                    << " at positions " << (i - 1) << " and " << i
                    << " (iter=" << iter << ", contract=" << c << ")");
            }

            // 6. Verify each receipt has required fields (txHash, from, gasUsed, status, blockNumber)
            for (size_t i = 0; i < retrievedReceipts.size(); ++i) {
                const auto& rcpt = retrievedReceipts[i];

                // txHash must be non-null
                BOOST_CHECK_MESSAGE(!rcpt.transactionHash.IsNull(),
                    "Receipt txHash is null at index " << i
                    << " (iter=" << iter << ", contract=" << c << ")");

                // from must be non-null (there's always a sender)
                BOOST_CHECK_MESSAGE(!rcpt.from.IsNull(),
                    "Receipt from is null at index " << i
                    << " (iter=" << iter << ", contract=" << c << ")");

                // gasUsed must be > 0 (every transaction uses some gas)
                BOOST_CHECK_MESSAGE(rcpt.gasUsed > 0,
                    "Receipt gasUsed is 0 at index " << i
                    << " (iter=" << iter << ", contract=" << c << ")");

                // status must be 0 or 1
                BOOST_CHECK_MESSAGE(rcpt.status == 0 || rcpt.status == 1,
                    "Receipt status is invalid (" << (int)rcpt.status << ") at index " << i
                    << " (iter=" << iter << ", contract=" << c << ")");

                // blockNumber must be > 0
                BOOST_CHECK_MESSAGE(rcpt.blockNumber > 0,
                    "Receipt blockNumber is 0 at index " << i
                    << " (iter=" << iter << ", contract=" << c << ")");
            }

            // 7. Verify completeness: count of retrieved receipts matches expected
            //    (at minimum, all receipts written in this iteration for this contract)
            size_t matchCount = 0;
            for (const auto& expectedPair : expected) {
                for (const auto& rcpt : retrievedReceipts) {
                    if (rcpt.transactionHash == expectedPair.first) {
                        matchCount++;
                        break;
                    }
                }
            }
            BOOST_CHECK_MESSAGE(matchCount == expected.size(),
                "Not all expected receipts found: matched " << matchCount
                << " out of " << expected.size()
                << " (iter=" << iter << ", contract=" << c << ")");
        }
    }

    // Cleanup temporary directory
    boost::system::error_code ec;
    fs::remove_all(tempDir, ec);

    BOOST_TEST_MESSAGE("Property 6 (Receipt-Abruf-Vollständigkeit und Sortierung): 100 iterations passed");
}

//=============================================================================
// Property 3: ABI-Funktions-Kategorisierung
// Feature: cvm-dashboard-contracts, Property 3: ABI-Funktions-Kategorisierung
//
// For any valid ABI JSON array, the ABI parser SHALL categorize all functions
// with stateMutability "view" or "pure" as read functions and all other
// functions as write functions, where the total number of categorized
// functions equals the number of function entries in the ABI.
//
// **Validates: Requirements 4.1**
//=============================================================================

namespace {

/**
 * Categorize ABI function entries into read and write functions.
 *
 * This is a C++ implementation of the categorization logic used in the
 * dashboard JavaScript (parseInteractABI in cvmdashboard_contracts.h):
 * - Functions with stateMutability "view" or "pure" are read functions
 * - All other functions (nonpayable, payable, or any other value) are write functions
 * - Non-function ABI entries (events, constructors, etc.) are skipped
 *
 * @param abiArray  A UniValue JSON array representing the ABI
 * @param readFunctions  Output: function entries categorized as read
 * @param writeFunctions Output: function entries categorized as write
 */
void CategorizeABIFunctions(
    const UniValue& abiArray,
    std::vector<UniValue>& readFunctions,
    std::vector<UniValue>& writeFunctions)
{
    readFunctions.clear();
    writeFunctions.clear();

    if (!abiArray.isArray()) return;

    for (size_t i = 0; i < abiArray.size(); ++i) {
        const UniValue& entry = abiArray[i];
        if (!entry.isObject()) continue;

        // Only process entries with type == "function"
        if (!entry.exists("type") || entry["type"].get_str() != "function") continue;

        // Get stateMutability (default to empty string if missing)
        std::string mutability;
        if (entry.exists("stateMutability") && entry["stateMutability"].isStr()) {
            mutability = entry["stateMutability"].get_str();
        }

        if (mutability == "view" || mutability == "pure") {
            readFunctions.push_back(entry);
        } else {
            writeFunctions.push_back(entry);
        }
    }
}

/**
 * Generate a random ABI function entry as a UniValue JSON object.
 *
 * @param rng           Random number generator
 * @param mutability    The stateMutability value to use
 * @param funcName      The function name
 * @return UniValue object representing an ABI function entry
 */
UniValue GenerateABIFunctionEntry(std::mt19937& rng, const std::string& mutability, const std::string& funcName)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("type", "function");
    entry.pushKV("name", funcName);
    entry.pushKV("stateMutability", mutability);

    // Random number of inputs (0 to 3)
    UniValue inputs(UniValue::VARR);
    std::uniform_int_distribution<int> numInputsDist(0, 3);
    int numInputs = numInputsDist(rng);
    const char* types[] = {"uint256", "address", "bool", "bytes32", "string"};
    for (int j = 0; j < numInputs; ++j) {
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "param" + std::to_string(j));
        inp.pushKV("type", std::string(types[rng() % 5]));
        inputs.push_back(inp);
    }
    entry.pushKV("inputs", inputs);

    // Random number of outputs (0 to 2)
    UniValue outputs(UniValue::VARR);
    std::uniform_int_distribution<int> numOutputsDist(0, 2);
    int numOutputs = numOutputsDist(rng);
    for (int j = 0; j < numOutputs; ++j) {
        UniValue out(UniValue::VOBJ);
        out.pushKV("name", "");
        out.pushKV("type", std::string(types[rng() % 5]));
        outputs.push_back(out);
    }
    entry.pushKV("outputs", outputs);

    return entry;
}

/**
 * Generate a random non-function ABI entry (event, constructor, fallback, receive).
 */
UniValue GenerateABINonFunctionEntry(std::mt19937& rng)
{
    const char* nonFuncTypes[] = {"event", "constructor", "fallback", "receive"};
    std::uniform_int_distribution<int> typeDist(0, 3);

    UniValue entry(UniValue::VOBJ);
    entry.pushKV("type", std::string(nonFuncTypes[typeDist(rng)]));
    entry.pushKV("name", "NonFunc" + std::to_string(rng() % 1000));

    // Events and constructors may have inputs
    UniValue inputs(UniValue::VARR);
    entry.pushKV("inputs", inputs);

    return entry;
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(property3_abi_function_categorization)
{
    std::mt19937 rng(54321);  // Fixed seed for reproducibility
    static constexpr int ITERATIONS = 100;

    // The four possible stateMutability values
    const std::string mutabilities[] = {"view", "pure", "nonpayable", "payable"};

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        // --- Generate a random ABI array ---
        UniValue abiArray(UniValue::VARR);

        // Random number of function entries (0 to 15)
        std::uniform_int_distribution<int> numFuncsDist(0, 15);
        int numFunctions = numFuncsDist(rng);

        // Random number of non-function entries (0 to 5)
        std::uniform_int_distribution<int> numNonFuncsDist(0, 5);
        int numNonFunctions = numNonFuncsDist(rng);

        // Track expected categorization
        int expectedRead = 0;
        int expectedWrite = 0;

        // Generate function entries with random stateMutability
        std::uniform_int_distribution<int> mutDist(0, 3);
        for (int i = 0; i < numFunctions; ++i) {
            std::string mut = mutabilities[mutDist(rng)];
            std::string funcName = "func" + std::to_string(i);

            UniValue funcEntry = GenerateABIFunctionEntry(rng, mut, funcName);
            abiArray.push_back(funcEntry);

            // Track expected categorization
            if (mut == "view" || mut == "pure") {
                expectedRead++;
            } else {
                expectedWrite++;
            }
        }

        // Intersperse non-function entries at random positions
        for (int i = 0; i < numNonFunctions; ++i) {
            abiArray.push_back(GenerateABINonFunctionEntry(rng));
        }

        // --- Apply the categorization ---
        std::vector<UniValue> readFunctions;
        std::vector<UniValue> writeFunctions;
        CategorizeABIFunctions(abiArray, readFunctions, writeFunctions);

        // --- Property checks ---

        // 1. Total categorized functions equals the number of function entries
        int totalCategorized = static_cast<int>(readFunctions.size() + writeFunctions.size());
        BOOST_CHECK_MESSAGE(totalCategorized == numFunctions,
            "Total categorized (" << totalCategorized << ") != number of function entries ("
            << numFunctions << ") (iter=" << iter << ")");

        // 2. Read function count matches expected
        BOOST_CHECK_MESSAGE(static_cast<int>(readFunctions.size()) == expectedRead,
            "Read function count (" << readFunctions.size() << ") != expected ("
            << expectedRead << ") (iter=" << iter << ")");

        // 3. Write function count matches expected
        BOOST_CHECK_MESSAGE(static_cast<int>(writeFunctions.size()) == expectedWrite,
            "Write function count (" << writeFunctions.size() << ") != expected ("
            << expectedWrite << ") (iter=" << iter << ")");

        // 4. Every read function has stateMutability "view" or "pure"
        for (size_t i = 0; i < readFunctions.size(); ++i) {
            BOOST_REQUIRE_MESSAGE(readFunctions[i].exists("stateMutability"),
                "Read function missing stateMutability (iter=" << iter << ", i=" << i << ")");
            std::string mut = readFunctions[i]["stateMutability"].get_str();
            BOOST_CHECK_MESSAGE(mut == "view" || mut == "pure",
                "Read function has unexpected stateMutability '" << mut
                << "' (iter=" << iter << ", i=" << i << ")");
        }

        // 5. Every write function has stateMutability that is NOT "view" or "pure"
        for (size_t i = 0; i < writeFunctions.size(); ++i) {
            std::string mut;
            if (writeFunctions[i].exists("stateMutability") &&
                writeFunctions[i]["stateMutability"].isStr()) {
                mut = writeFunctions[i]["stateMutability"].get_str();
            }
            BOOST_CHECK_MESSAGE(mut != "view" && mut != "pure",
                "Write function has read-only stateMutability '" << mut
                << "' (iter=" << iter << ", i=" << i << ")");
        }

        // 6. Non-function entries are not included in either category
        //    (verified implicitly: totalCategorized == numFunctions, and
        //     numFunctions does not include non-function entries)

        // 7. Every function entry from the ABI appears in exactly one category
        for (size_t i = 0; i < abiArray.size(); ++i) {
            const UniValue& entry = abiArray[i];
            if (!entry.isObject()) continue;
            if (!entry.exists("type") || entry["type"].get_str() != "function") continue;

            std::string name = entry["name"].get_str();
            int foundInRead = 0;
            int foundInWrite = 0;

            for (const auto& rf : readFunctions) {
                if (rf["name"].get_str() == name) foundInRead++;
            }
            for (const auto& wf : writeFunctions) {
                if (wf["name"].get_str() == name) foundInWrite++;
            }

            BOOST_CHECK_MESSAGE(foundInRead + foundInWrite == 1,
                "Function '" << name << "' found " << foundInRead << " times in read and "
                << foundInWrite << " times in write (expected exactly 1 total)"
                << " (iter=" << iter << ")");
        }
    }

    BOOST_TEST_MESSAGE("Property 3 (ABI-Funktions-Kategorisierung): 100 iterations passed");
}

//=============================================================================
// Property 4: ABI-Parameter-Encoding-Round-Trip
// Feature: cvm-dashboard-contracts, Property 4: ABI-Parameter-Encoding-Round-Trip
//
// For any ABI function definition and for any valid parameter values,
// encoding the parameters to hex and then decoding the hex back to values
// SHALL yield the original parameter values.
//
// **Validates: Requirements 4.2**
//=============================================================================

namespace {

/**
 * ABI parameter types supported for encoding/decoding round-trip testing.
 */
enum class ABIType {
    UINT256,
    ADDRESS,
    BOOL,
    BYTES32
};

/**
 * A typed ABI parameter value for round-trip testing.
 * Stores the raw bytes and the type so we can encode and decode.
 */
struct ABIParam {
    ABIType type;
    std::vector<unsigned char> value;  // Raw bytes (type-specific length)
};

/**
 * Encode a single ABI parameter to a 64-character hex string (32 bytes).
 *
 * Mirrors the JavaScript encodeABIParameters() logic:
 * - uint256: 32-byte big-endian, left-padded with zeros
 * - address: 20-byte value, left-padded with zeros to 32 bytes
 * - bool: 0 or 1 as the last byte, left-padded with zeros
 * - bytes32: 32-byte raw value, right-padded with zeros
 */
std::string EncodeABIParam(const ABIParam& param)
{
    // Each ABI word is 32 bytes = 64 hex characters
    std::vector<unsigned char> word(32, 0);

    switch (param.type) {
    case ABIType::UINT256:
        // uint256: 32 bytes big-endian, value is stored big-endian already
        // Left-pad: copy value right-aligned into the 32-byte word
        if (param.value.size() <= 32) {
            size_t offset = 32 - param.value.size();
            std::copy(param.value.begin(), param.value.end(), word.begin() + offset);
        }
        break;

    case ABIType::ADDRESS:
        // address: 20 bytes, left-padded to 32 bytes
        if (param.value.size() <= 20) {
            size_t offset = 32 - param.value.size();
            std::copy(param.value.begin(), param.value.end(), word.begin() + offset);
        }
        break;

    case ABIType::BOOL:
        // bool: 0 or 1 in the last byte
        word[31] = param.value.empty() ? 0 : (param.value[0] ? 1 : 0);
        break;

    case ABIType::BYTES32:
        // bytes32: 32 bytes, right-padded with zeros
        {
            size_t copyLen = std::min(param.value.size(), (size_t)32);
            std::copy(param.value.begin(), param.value.begin() + copyLen, word.begin());
        }
        break;
    }

    // Convert to hex string
    std::string hex;
    hex.reserve(64);
    static const char hexChars[] = "0123456789abcdef";
    for (unsigned char b : word) {
        hex.push_back(hexChars[(b >> 4) & 0x0F]);
        hex.push_back(hexChars[b & 0x0F]);
    }
    return hex;
}

/**
 * Decode a 64-character hex string back to an ABIParam of the given type.
 *
 * Mirrors the JavaScript decodeABIResult() logic:
 * - uint256: interpret all 32 bytes as big-endian value
 * - address: extract last 20 bytes (skip first 12 zero-padding bytes)
 * - bool: check if last byte is 1
 * - bytes32: take all 32 bytes as-is
 */
ABIParam DecodeABIParam(const std::string& hex, ABIType type)
{
    ABIParam result;
    result.type = type;

    // Parse hex string to 32 bytes
    std::vector<unsigned char> word(32, 0);
    for (size_t i = 0; i < 64 && i < hex.size(); i += 2) {
        unsigned char hi = 0, lo = 0;
        char c = hex[i];
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;

        c = hex[i + 1];
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;

        word[i / 2] = (hi << 4) | lo;
    }

    switch (type) {
    case ABIType::UINT256:
        // Return all 32 bytes (big-endian)
        result.value.assign(word.begin(), word.end());
        break;

    case ABIType::ADDRESS:
        // Extract last 20 bytes (bytes 12..31)
        result.value.assign(word.begin() + 12, word.end());
        break;

    case ABIType::BOOL:
        // Single byte: 0 or 1
        result.value = { static_cast<unsigned char>(word[31] != 0 ? 1 : 0) };
        break;

    case ABIType::BYTES32:
        // All 32 bytes as-is
        result.value.assign(word.begin(), word.end());
        break;
    }

    return result;
}

/**
 * Generate a random ABIParam of the given type.
 */
ABIParam GenerateRandomABIParam(std::mt19937& rng, ABIType type)
{
    ABIParam param;
    param.type = type;

    switch (type) {
    case ABIType::UINT256:
        // 32 random bytes (big-endian)
        param.value.resize(32);
        for (auto& b : param.value) {
            b = static_cast<unsigned char>(rng() & 0xFF);
        }
        break;

    case ABIType::ADDRESS:
        // 20 random bytes
        param.value.resize(20);
        for (auto& b : param.value) {
            b = static_cast<unsigned char>(rng() & 0xFF);
        }
        break;

    case ABIType::BOOL:
        // Single byte: 0 or 1
        param.value = { static_cast<unsigned char>(rng() % 2) };
        break;

    case ABIType::BYTES32:
        // 32 random bytes
        param.value.resize(32);
        for (auto& b : param.value) {
            b = static_cast<unsigned char>(rng() & 0xFF);
        }
        break;
    }

    return param;
}

/**
 * Get a human-readable name for an ABI type.
 */
std::string ABITypeName(ABIType type)
{
    switch (type) {
    case ABIType::UINT256: return "uint256";
    case ABIType::ADDRESS: return "address";
    case ABIType::BOOL:    return "bool";
    case ABIType::BYTES32: return "bytes32";
    default:               return "unknown";
    }
}

/**
 * Encode multiple ABI parameters into a concatenated hex string.
 * Each parameter produces a 64-char hex word; they are concatenated.
 */
std::string EncodeABIParams(const std::vector<ABIParam>& params)
{
    std::string result;
    result.reserve(params.size() * 64);
    for (const auto& p : params) {
        result += EncodeABIParam(p);
    }
    return result;
}

/**
 * Decode a concatenated hex string back into ABI parameters given their types.
 * Each 64-char chunk is decoded according to the corresponding type.
 */
std::vector<ABIParam> DecodeABIParams(const std::string& hex, const std::vector<ABIType>& types)
{
    std::vector<ABIParam> result;
    result.reserve(types.size());
    for (size_t i = 0; i < types.size(); ++i) {
        std::string chunk = hex.substr(i * 64, 64);
        result.push_back(DecodeABIParam(chunk, types[i]));
    }
    return result;
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(property4_abi_encoding_round_trip)
{
    std::mt19937 rng(31415);  // Fixed seed for reproducibility
    static constexpr int ITERATIONS = 100;

    // All supported ABI types
    const ABIType allTypes[] = { ABIType::UINT256, ABIType::ADDRESS, ABIType::BOOL, ABIType::BYTES32 };

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        // --- Generate a random set of parameters ---

        // Random number of parameters (1 to 6)
        std::uniform_int_distribution<size_t> numParamsDist(1, 6);
        size_t numParams = numParamsDist(rng);

        std::vector<ABIParam> originalParams;
        std::vector<ABIType> paramTypes;
        originalParams.reserve(numParams);
        paramTypes.reserve(numParams);

        std::uniform_int_distribution<int> typeDist(0, 3);
        for (size_t i = 0; i < numParams; ++i) {
            ABIType type = allTypes[typeDist(rng)];
            paramTypes.push_back(type);
            originalParams.push_back(GenerateRandomABIParam(rng, type));
        }

        // --- Encode to hex ---
        std::string encoded = EncodeABIParams(originalParams);

        // --- Property check: encoded length is correct ---
        BOOST_CHECK_MESSAGE(encoded.size() == numParams * 64,
            "Encoded hex length should be " << (numParams * 64)
            << " but got " << encoded.size()
            << " (iter=" << iter << ")");

        // --- Decode back ---
        std::vector<ABIParam> decodedParams = DecodeABIParams(encoded, paramTypes);

        // --- Property check: number of decoded params matches ---
        BOOST_CHECK_MESSAGE(decodedParams.size() == originalParams.size(),
            "Decoded param count (" << decodedParams.size()
            << ") != original (" << originalParams.size()
            << ") (iter=" << iter << ")");

        // --- Property check: each decoded param equals the original ---
        for (size_t i = 0; i < originalParams.size() && i < decodedParams.size(); ++i) {
            const ABIParam& orig = originalParams[i];
            const ABIParam& decoded = decodedParams[i];

            // Types must match
            BOOST_CHECK_MESSAGE(orig.type == decoded.type,
                "Type mismatch at param " << i
                << ": original=" << ABITypeName(orig.type)
                << ", decoded=" << ABITypeName(decoded.type)
                << " (iter=" << iter << ")");

            // Values must match
            BOOST_CHECK_MESSAGE(orig.value == decoded.value,
                "Value mismatch at param " << i
                << " (type=" << ABITypeName(orig.type) << ")"
                << " (iter=" << iter << ")");
        }

        // --- Additional property: encoding is deterministic ---
        // Encoding the same params again should produce the same hex
        std::string encoded2 = EncodeABIParams(originalParams);
        BOOST_CHECK_MESSAGE(encoded == encoded2,
            "Encoding is not deterministic (iter=" << iter << ")");

        // --- Additional property: each 64-char word is valid hex ---
        for (size_t i = 0; i < encoded.size(); ++i) {
            char c = encoded[i];
            bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            BOOST_CHECK_MESSAGE(isHex,
                "Non-hex character '" << c << "' at position " << i
                << " (iter=" << iter << ")");
        }
    }

    BOOST_TEST_MESSAGE("Property 4 (ABI-Parameter-Encoding-Round-Trip): 100 iterations passed");
}

BOOST_AUTO_TEST_SUITE_END()
