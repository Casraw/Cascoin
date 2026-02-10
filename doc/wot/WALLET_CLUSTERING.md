# Cascoin Wallet Clustering System

## 🎯 Purpose

The Wallet Clustering System prevents reputation gaming by linking addresses that belong to the same wallet. This is a **critical anti-Sybil mechanism** that stops attackers from:

- Creating new addresses to escape bad reputation
- Using "clean" addresses while hiding scammer addresses
- Gaming the reputation system by fragmenting their identity

## 🔍 How It Works

### Chain Analysis Heuristics

The system uses proven blockchain analysis techniques to identify addresses controlled by the same entity:

#### 1. **Common Input Heuristic** 
If multiple addresses appear as inputs in the same transaction, they likely belong to the same wallet (since only one entity can sign all inputs).

```
Transaction:
  Inputs: [Address_A, Address_B, Address_C]
  Outputs: [...]
  
→ Conclusion: A, B, C belong to same wallet
```

#### 2. **Change Address Detection**
In a 2-output transaction, the smaller output is often the change address, which belongs to the sender's wallet.

```
Transaction:
  Inputs: [Address_A]
  Outputs: [Address_X: 50 CAS, Address_B: 1.5 CAS]
  
→ Conclusion: Address_B is likely change, belongs to Address_A's wallet
```

#### 3. **Union-Find Clustering**
All detected relationships are merged using an efficient Union-Find algorithm to create wallet clusters.

## 🛡️ Security Benefits

### Prevention of "Fresh Start" Attacks

**Without Clustering:**
```
Scammer_Address_1: Reputation = -100 (reported as scammer)
↓ Creates new address
Scammer_Address_2: Reputation = 0 (clean slate!)
→ Scammer can trade again ❌
```

**With Clustering:**
```
Scammer_Address_1: Reputation = -100
Scammer_Address_2: Reputation = -100 (linked via clustering)
→ Scammer CANNOT escape bad reputation ✅
```

### Conservative Reputation Sharing

The system uses the **MINIMUM reputation** across all addresses in a wallet cluster:

```
Wallet Cluster:
  - Address_A: Reputation = 80
  - Address_B: Reputation = 95
  - Address_C: Reputation = 30  ← Lowest!
  
→ Effective Reputation for ALL addresses = 30
```

This conservative approach prevents attackers from using one "clean" address to mask the bad reputation of others.

## 📡 RPC Commands

### 1. `buildwalletclusters`

Analyze the entire blockchain and build wallet clusters.

```bash
cascoin-cli buildwalletclusters
```

**Response:**
```json
{
  "total_clusters": 1523,
  "largest_cluster": 47,
  "status": "Wallet clusters built successfully"
}
```

**When to use:** 
- After initial sync
- Periodically (e.g. weekly) to update clusters
- After significant network activity

---

### 2. `getwalletcluster "address"`

Get all addresses in the same wallet cluster as a given address.

```bash
cascoin-cli getwalletcluster "QAddress123..."
```

**Response:**
```json
{
  "cluster_id": "QAddress123...",
  "member_count": 5,
  "members": [
    "QAddress123...",
    "QAddress456...",
    "QAddress789...",
    "QAddressABC...",
    "QAddressDEF..."
  ],
  "shared_reputation": 45.5,
  "shared_hat_score": 38.2
}
```

**Use case:** 
- Check if an address has other linked addresses
- Investigate potential multi-address operations
- Verify wallet clustering is working correctly

---

### 3. `geteffectivetrust "target" ["viewer"]`

Get the effective HAT v2 trust score considering wallet clustering.

```bash
cascoin-cli geteffectivetrust "QTarget..." "QViewer..."
```

**Response:**
```json
{
  "target": "QTarget...",
  "cluster_id": "QCluster...",
  "cluster_size": 3,
  "individual_score": 75.5,
  "effective_score": 42.0,
  "penalty_applied": true,
  "worst_address_in_cluster": "QBadAddress...",
  "worst_score": 42.0
}
```

**Key fields:**
- `individual_score`: Score for this address alone
- `effective_score`: Minimum score across entire wallet cluster
- `penalty_applied`: `true` if cluster lowered the score
- `worst_address_in_cluster`: Which address has the lowest score

**Use case:**
- **ALWAYS use this instead of `getsecuretrust` for trading decisions!**
- Assess real risk by considering entire wallet history
- Identify hidden bad actors using "clean" addresses

## 🎮 Usage Examples

### Example 1: Detecting a Scammer's Alt Accounts

```bash
# 1. User reports QScammer123 has negative reputation
cascoin-cli getreputation "QScammer123"
# → Reputation: -80

# 2. Check wallet cluster
cascoin-cli getwalletcluster "QScammer123"
# → Found 4 linked addresses!

# 3. Check effective trust of a "clean looking" address
cascoin-cli geteffectivetrust "QScammer456"
# → effective_score: -80 (cluster penalty applied!)
# → worst_address_in_cluster: "QScammer123"

# Result: All scammer's addresses now have bad reputation ✅
```

### Example 2: Trading Decision

```bash
# Before accepting a 100 CAS trade from QTrader789:

# ❌ WRONG WAY (can be gamed):
cascoin-cli getsecuretrust "QTrader789"
# → Score: 85 (looks good!)

# ✅ CORRECT WAY (cluster-aware):
cascoin-cli geteffectivetrust "QTrader789"
# → individual_score: 85
# → effective_score: 15  ← Cluster has bad address!
# → worst_address_in_cluster: "QTraderAlt"
# → penalty_applied: true

# Decision: REJECT trade! Address is linked to bad actor.
```

### Example 3: Monitoring Cluster Growth

```bash
# Track how many addresses a wallet has
cascoin-cli getwalletcluster "QMyAddress"
# → member_count: 3

# After some transactions...
cascoin-cli getwalletcluster "QMyAddress"
# → member_count: 5  (detected 2 more linked addresses)
```

## 🔧 Integration with HAT v2

The Wallet Clustering System is designed to integrate seamlessly with HAT v2 (Hybrid Adaptive Trust):

1. **HAT v2 calculates individual trust** for each address
2. **Wallet Clustering groups addresses** into wallet clusters
3. **Effective trust is the MINIMUM** across the cluster
4. **Trading decisions use effective trust**, not individual trust

## 📊 Technical Details

### Data Structures

```cpp
struct WalletClusterInfo {
    uint160 cluster_id;                   // Primary address (root)
    std::set<uint160> member_addresses;    // All addresses in cluster
    int64_t first_seen;                    // Oldest address timestamp
    int64_t last_activity;                 // Most recent transaction
    uint32_t transaction_count;            // Total transactions
    double shared_reputation;              // Minimum reputation
};
```

### Algorithm Complexity

- **Clustering**: O(N * M) where N = transactions, M = inputs per tx
- **Cluster Lookup**: O(log N) with path compression
- **Union Operation**: O(α(N)) ≈ O(1) amortized (inverse Ackermann)

### Storage

Clusters are persisted to the CVM database:
- Key format: `wc_<cluster_id>` → `WalletClusterInfo`
- Address mapping: `wca_<address>` → `cluster_id`

## ⚙️ Configuration

### Rebuilding Clusters

Clusters should be rebuilt periodically:

```bash
# Weekly cron job:
0 2 * * 0 cascoin-cli buildwalletclusters
```

### Automatic Incremental Updates

(Future enhancement) Clusters will be updated automatically as new blocks are processed.

## 🚀 Best Practices

### For Users

1. **Always use `geteffectivetrust`** before trading
2. **Check cluster size** - large clusters may indicate exchange/mixer
3. **Look for `penalty_applied`** - investigate if true
4. **Verify `worst_address`** to understand cluster reputation

### For Developers/Integrators

1. **Use effective trust in all reputation checks**
2. **Display cluster information in UI**
3. **Warn users about cluster penalties**
4. **Update clusters after significant blockchain growth**

### For Exchanges

1. **Build clusters on initial sync**
2. **Monitor large clusters** (may be other exchanges)
3. **Flag addresses with cluster penalties**
4. **Use effective trust for withdrawal risk assessment**

## 🎯 Example Attack Scenarios

### Scenario 1: Sybil Attack Prevention

**Attack:** Create 100 fake addresses, trade between them to build reputation

**Defense:**
- Common input heuristic links all addresses
- Cluster detected as single entity
- Suspicious pattern detected in behavior metrics
- HAT v2 applies cluster penalty

### Scenario 2: Reputation Laundering

**Attack:** 
1. Scam users with Address_A (reputation → -100)
2. Create new Address_B
3. Use Address_B for clean slate

**Defense:**
- Any transaction involving both addresses links them
- Address_B inherits Address_A's bad reputation
- `geteffectivetrust` reveals the connection
- Trading partners see the penalty

### Scenario 3: Split-Merge Attack

**Attack:**
1. Build good reputation on Address_A (score = 90)
2. Scam with Address_B (score = -50)
3. Keep addresses separate
4. Use Address_A for trading

**Defense:**
- Eventually both addresses used in same transaction (common input)
- Wallet cluster formed: {A, B}
- Effective score = min(90, -50) = -50
- Address_A now untrusted

## 📈 Statistics

Track clustering effectiveness:

```bash
# Get clustering statistics
cascoin-cli getwalletcluster "QAnyAddress" | jq .

# Count total clusters
cascoin-cli buildwalletclusters | jq .total_clusters

# Check largest cluster (may be exchange)
cascoin-cli buildwalletclusters | jq .largest_cluster
```

## ⚠️ Limitations

1. **False Positives:** CoinJoin/mixing services may link unrelated addresses
2. **Privacy Trade-off:** Cluster analysis reduces privacy
3. **Computational Cost:** Initial clustering scan is expensive
4. **Delayed Detection:** New addresses only detected after first shared transaction

## 🔐 Privacy Considerations

While clustering reduces privacy, it's a **necessary trade-off** for security:

- **Pro:** Prevents reputation gaming
- **Pro:** Protects honest users from scammers
- **Con:** Links user's addresses together
- **Mitigation:** Users can use separate wallets for separate identities

## 📚 References

- [Bitcoin Address Clustering](https://en.bitcoin.it/wiki/Privacy#Address_reuse)
- [Chain Analysis Techniques](https://cseweb.ucsd.edu/~smeiklejohn/files/imc13.pdf)
- HAT v2 Documentation: `doc/HAT_V2_USER_GUIDE.md`

---

## 🎓 Summary

The Cascoin Wallet Clustering System is a **critical component** of the reputation system that prevents attackers from escaping bad reputation by creating new addresses. By analyzing blockchain transactions and linking addresses that are controlled by the same entity, it ensures that reputation is **wallet-based, not address-based**.

**Key Takeaway:** Always use `geteffectivetrust` instead of `getsecuretrust` to get the true risk assessment that considers wallet clustering!

