# Web-of-Trust Reputation System for Cascoin

## 🌐 Overview

The Cascoin Web-of-Trust (WoT) system is a **personalized, decentralized reputation framework** that replaces simple global scores with individualized trust graphs. Each user maintains their own trust relationships, and reputation is calculated based on **trust paths** through the network.

---

## 🎯 Core Concepts

### 1. **Trust Graph**
- Each user maintains a personal trust graph
- Trust edges connect addresses: `A trusts B with weight X`
- Trust is **directional** and **weighted** (-100 to +100)
- Trust relationships require **bonding CAS** to prevent spam

### 2. **Personalized Reputation**
Unlike global reputation systems, WoT reputation is **viewer-dependent**:
- Alice's view of Charlie's reputation ≠ Bob's view of Charlie
- Reputation is weighted by **trust paths** from viewer to target
- Stronger trust paths = more influence on reputation

### 3. **Trust Paths**
A trust path is a sequence of trust relationships:
```
Alice --80%--> Bob --90%--> Charlie
```
- Path strength = product of weights: `0.8 * 0.9 = 0.72`
- Multiple paths aggregate for final reputation
- Max path depth configurable (default: 3 hops)

### 4. **Bonding & Slashing**
- **Bonding**: Votes/trust require staking CAS
  - Min bond: 1 CAS
  - Additional bond: 0.01 CAS per vote point
  - Example: Vote of +80 requires 1.80 CAS bond
- **Slashing**: DAO can slash bonds for malicious votes
  - Bonded CAS is burned/redistributed
  - Slashed votes are ignored in reputation calculation

### 5. **DAO Arbitration**
- DAO panel can arbitrate disputes
- Members stake CAS to vote on disputes
- Weighted voting by stake
- Minimum quorum required
- Auto-resolution after timeout

---

## 📐 System Architecture

### Data Structures

```
TrustEdge:
  - fromAddress: Who is trusting
  - toAddress: Who is trusted
  - trustWeight: Trust level (-100 to +100)
  - bondAmount: CAS bonded
  - slashed: Was bond slashed?

BondedVote:
  - voter: Who voted
  - target: Who was voted on
  - voteValue: Vote (-100 to +100)
  - bondAmount: CAS bonded
  - slashed: Was bond slashed?

TrustPath:
  - addresses: [A, B, C, D]
  - weights: [80, 90, 70]
  - totalWeight: 0.504 (0.8 * 0.9 * 0.7)

DAODispute:
  - disputeId: Unique ID
  - originalVoteTx: Vote being disputed
  - daoVotes: {member -> vote}
  - daoStakes: {member -> stake}
  - resolved: Is it resolved?
  - slashDecision: Outcome
```

### Storage

All data stored in LevelDB under CVM database:
- `trust_{from}_{to}` → TrustEdge
- `trust_in_{to}_{from}` → TrustEdge (reverse index)
- `vote_{txhash}` → BondedVote
- `votes_{target}_{txhash}` → BondedVote (target index)
- `dispute_{id}` → DAODispute

---

## 🚀 Usage Examples

### 1. Add Trust Relationship

```bash
# Alice trusts Bob with 80% confidence
cascoin-cli addtrust "QbobAddress123..." 80 2.0 "Known personally, reliable"
```

**Result:**
```json
{
  "from": "QaliceAddress456...",
  "to": "QbobAddress123...",
  "weight": 80,
  "bond": 2.00000000,
  "required_bond": 1.80000000,
  "reason": "Known personally, reliable"
}
```

### 2. Get Weighted Reputation

```bash
# Get Charlie's reputation from Alice's perspective
cascoin-cli getweightedreputation "QcharlieAddr789..." "QaliceAddress456..." 3
```

**Result:**
```json
{
  "target": "QcharlieAddr789...",
  "viewer": "QaliceAddress456...",
  "reputation": 68.5,
  "paths_found": 2,
  "max_depth": 3,
  "paths": [
    {
      "length": 2,
      "weight": 0.72,
      "hops": [
        {"address": "QbobAddress123...", "trust_weight": 80},
        {"address": "QcharlieAddr789...", "trust_weight": 90}
      ]
    },
    {
      "length": 3,
      "weight": 0.504,
      "hops": [
        {"address": "QdaveAddr321...", "trust_weight": 70},
        {"address": "QeveAddr654...", "trust_weight": 80},
        {"address": "QcharlieAddr789...", "trust_weight": 90}
      ]
    }
  ]
}
```

### 3. Get Graph Statistics

```bash
cascoin-cli gettrustgraphstats
```

**Result:**
```json
{
  "total_trust_edges": 1247,
  "total_votes": 3891,
  "total_disputes": 23,
  "active_disputes": 3,
  "slashed_votes": 15,
  "min_bond_amount": 1.00000000,
  "bond_per_vote_point": 0.01000000,
  "max_trust_path_depth": 3,
  "min_dao_votes": 5
}
```

---

## 🔄 Reputation Calculation Algorithm

```python
def calculate_weighted_reputation(viewer, target, max_depth=3):
    # 1. Find all trust paths from viewer to target
    paths = find_trust_paths(viewer, target, max_depth)
    
    if len(paths) == 0:
        # No trust path - use unweighted global reputation
        return calculate_global_reputation(target)
    
    # 2. Get all votes for target
    votes = get_votes_for_address(target)
    
    # 3. Weight each vote by trust path strength
    weighted_sum = 0.0
    total_weight = 0.0
    
    for vote in votes:
        if vote.slashed:
            continue  # Skip slashed votes
        
        for path in paths:
            path_weight = path.total_weight
            weighted_sum += vote.value * path_weight
            total_weight += path_weight
    
    # 4. Return weighted average
    return weighted_sum / total_weight if total_weight > 0 else 0.0
```

### Example Calculation

**Network:**
- Alice trusts Bob (80%)
- Bob trusts Charlie (90%)
- Votes for Charlie: +100, +50, -20

**Alice's view of Charlie:**
```
Path: Alice -> Bob -> Charlie
Weight: 0.8 * 0.9 = 0.72

Weighted reputation:
  (+100 * 0.72) + (+50 * 0.72) + (-20 * 0.72) / (0.72 + 0.72 + 0.72)
  = (72 + 36 - 14.4) / 2.16
  = 93.6 / 2.16
  = 43.3
```

**Dave's view of Charlie** (no trust path):
```
Global average: (+100 + 50 - 20) / 3 = 43.3
```

---

## 🛡️ Bonding & Slashing

### Bond Requirements

| Vote Value | Min Bond | Total Bond |
|------------|----------|------------|
| ±10        | 1.00 CAS | 1.10 CAS   |
| ±50        | 1.00 CAS | 1.50 CAS   |
| ±100       | 1.00 CAS | 2.00 CAS   |

Formula: `bond = min_bond + (abs(vote) * bond_per_point)`

### Slashing Process

1. **Challenge**: User submits challenge with bond
2. **DAO Vote**: DAO members stake and vote
3. **Resolution**: After minimum votes or timeout
4. **Execution**: If slash decision, bond is burned

---

## 🏛️ DAO Governance

### DAO Membership
- Minimum stake requirement
- Governance token holding (future)
- Reputation threshold (future)

### Dispute Resolution
1. Challenger stakes CAS
2. DAO members review evidence
3. DAO members stake and vote (slash/keep)
4. Weighted by stake
5. Quorum: 51% of total DAO stake
6. Auto-resolve after 1440 blocks (~1 day)

### DAO Vote Example

**Dispute #123:**
- Challenger: Alice (bond: 2 CAS)
- Original vote: Bob voted +100 for Charlie
- Reason: "Charlie is a known scammer"

**DAO Votes:**
| Member | Vote  | Stake  |
|--------|-------|--------|
| Dave   | Slash | 10 CAS |
| Eve    | Keep  | 5 CAS  |
| Frank  | Slash | 8 CAS  |
| Grace  | Slash | 7 CAS  |

**Outcome:**
- Total stake support slash: 25 CAS
- Total stake oppose slash: 5 CAS
- **Decision: SLASH** (83% support)
- Bob's 2 CAS bond is burned
- Bob's vote is marked as slashed
- Future reputation calculations ignore this vote

---

## 🔧 Configuration

```cpp
struct WoTConfig {
    CAmount minBondAmount;          // 1 CAS
    CAmount bondPerVotePoint;       // 0.01 CAS
    int maxTrustPathDepth;          // 3 hops
    int minDAOVotesForResolution;   // 5 votes
    double daoQuorumPercentage;     // 51%
    uint32_t disputeTimeoutBlocks;  // 1440 blocks (~1 day)
};
```

---

## 📊 Benefits Over Simple Scores

### Traditional System:
- ❌ Global reputation (one-size-fits-all)
- ❌ Sybil attack vulnerable
- ❌ No context or relationships
- ❌ Easily manipulated

### Web-of-Trust:
- ✅ **Personalized** reputation per viewer
- ✅ **Sybil-resistant** (requires trust paths)
- ✅ **Context-aware** (who you trust matters)
- ✅ **Attack-resistant** (bonds + DAO)
- ✅ **Decentralized** (no central authority)

---

## 🚦 Integration with Existing System

### Backwards Compatibility
- Old `getreputation` → global average (unweighted)
- Old `votereputation` → simple vote (no bonding)
- New `getweightedreputation` → WoT personalized
- New `addtrust` → WoT trust edges

### Migration Path
1. **Phase 1**: Deploy WoT alongside simple system
2. **Phase 2**: Users build trust graphs
3. **Phase 3**: UIs switch to weighted reputation
4. **Phase 4**: Deprecate simple system

---

## 🎯 Use Cases

### 1. **Marketplace Trust**
- Buyer checks seller reputation **from their perspective**
- Influenced by who buyer trusts
- More relevant than global score

### 2. **Community Moderation**
- Community leaders establish trust networks
- Spam/scam reports weighted by trust
- Resistant to false reports

### 3. **Financial Services**
- Credit scoring based on trust network
- Loan defaults impact reputation within network
- More accurate than global credit scores

### 4. **Identity Verification**
- KYC attestations from trusted parties
- Reputation builds through verified connections
- Resistant to fake identities

---

## 📝 RPC Commands Summary

| Command | Description | Category |
|---------|-------------|----------|
| `addtrust` | Add trust relationship | WoT |
| `getweightedreputation` | Get personalized reputation | WoT |
| `gettrustgraphstats` | Get graph statistics | WoT |
| `getreputation` | Get simple global reputation | Legacy |
| `votereputation` | Cast simple vote (no bond) | Legacy |
| `sendcvmvote` | Send on-chain bonded vote | New |

---

## 🔮 Future Enhancements

1. **Automatic Trust Propagation**
   - If A trusts B (90%) and B trusts C (80%), suggest A->C (72%)

2. **Trust Decay**
   - Trust edges weaken over time without reinforcement

3. **Multi-dimensional Trust**
   - Technical trust vs. financial trust vs. social trust

4. **Privacy-Preserving WoT**
   - Zero-knowledge proofs for trust relationships
   - Private trust graphs with public reputation

5. **Governance Token Integration**
   - DAO voting power based on governance tokens
   - Staking rewards for good DAO participation

---

## 🏁 Conclusion

The Web-of-Trust reputation system transforms Cascoin into a **context-aware, personalized, and attack-resistant** reputation platform. By combining trust graphs, bonding, and DAO governance, it provides a **truly decentralized** alternative to centralized reputation systems.

**Key Innovation**: Reputation is not global truth, but **personal perspective** based on your trust network! 🌐

