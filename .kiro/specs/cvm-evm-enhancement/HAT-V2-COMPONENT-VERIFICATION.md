# HAT v2 Component-Based Verification Logic

## Problem Statement

The original HAT v2 consensus design used a simple ±5 point tolerance for all validators, regardless of whether they had a Web-of-Trust (WoT) connection to the transaction sender. This approach had critical flaws:

1. **Validators without WoT connection** couldn't accurately verify the WoT component (40% of total score)
2. **Fixed tolerance** didn't account for the fact that different components have different variance characteristics
3. **Unfair rejection** of valid transactions when WoT perspectives legitimately differ

## Improved Solution: Component-Based Verification

### Core Principle

**Validators should only verify components they can accurately calculate.**

- Validators **WITH** WoT connection → Verify ALL 4 components
- Validators **WITHOUT** WoT connection → Verify ONLY non-WoT components (Behavior, Economic, Temporal)

### HAT v2 Score Components

```
Total Score (0-100) = WoT (40%) + Behavior (30%) + Economic (20%) + Temporal (10%)

Example:
- WoT Score: 34 points (40% weight)
- Behavior Score: 26 points (30% weight)
- Economic Score: 17 points (20% weight)
- Temporal Score: 8 points (10% weight)
- Final Score: 85 points
```

### Component Characteristics

| Component | Weight | Variance | Reason for Variance |
|-----------|--------|----------|---------------------|
| **WoT** | 40% | HIGH | Personalized trust graph, different perspectives |
| **Behavior** | 30% | LOW | Objective on-chain data, same for all validators |
| **Economic** | 20% | LOW | Objective stake/transaction data |
| **Temporal** | 10% | LOW | Objective time-based metrics |

### Verification Logic

#### For Validators WITH WoT Connection

```cpp
bool VerifyWithWoT(HATv2Score selfReported, HATv2Score calculated) {
    // Verify each component individually
    bool wotMatch = abs(selfReported.wotScore - calculated.wotScore) <= 5;
    bool behaviorMatch = abs(selfReported.behaviorScore - calculated.behaviorScore) <= 3;
    bool economicMatch = abs(selfReported.economicScore - calculated.economicScore) <= 3;
    bool temporalMatch = abs(selfReported.temporalScore - calculated.temporalScore) <= 3;
    
    // All components must match
    if (!wotMatch || !behaviorMatch || !economicMatch || !temporalMatch) {
        return REJECT;
    }
    
    // Also verify final score (with higher tolerance for accumulated rounding)
    bool finalMatch = abs(selfReported.finalScore - calculated.finalScore) <= 8;
    
    return finalMatch ? ACCEPT : REJECT;
}
```

**Tolerances:**
- WoT: ±5 points (accounts for different trust graph perspectives)
- Behavior: ±3 points (objective data, less variance)
- Economic: ±3 points (objective data, less variance)
- Temporal: ±3 points (objective data, less variance)
- Final Score: ±8 points (accumulated rounding from components)

**Vote Confidence:** 1.0 (full verification capability)

#### For Validators WITHOUT WoT Connection

```cpp
bool VerifyWithoutWoT(HATv2Score selfReported, HATv2Score calculated) {
    // IGNORE WoT component entirely
    // Only verify non-WoT components
    
    bool behaviorMatch = abs(selfReported.behaviorScore - calculated.behaviorScore) <= 3;
    bool economicMatch = abs(selfReported.economicScore - calculated.economicScore) <= 3;
    bool temporalMatch = abs(selfReported.temporalScore - calculated.temporalScore) <= 3;
    
    // All verifiable components must match
    if (!behaviorMatch || !economicMatch || !temporalMatch) {
        return REJECT;
    }
    
    // Calculate adjusted final score (excluding WoT component)
    // Non-WoT components represent 60% of total weight
    double adjustedSelfReported = (selfReported.behaviorScore + 
                                   selfReported.economicScore + 
                                   selfReported.temporalScore);
    double adjustedCalculated = (calculated.behaviorScore + 
                                calculated.economicScore + 
                                calculated.temporalScore);
    
    // Verify adjusted score (60% of total)
    bool adjustedMatch = abs(adjustedSelfReported - adjustedCalculated) <= 5;
    
    return adjustedMatch ? ACCEPT : REJECT;
}
```

**Tolerances:**
- WoT: **IGNORED** (cannot verify)
- Behavior: ±3 points
- Economic: ±3 points
- Temporal: ±3 points
- Adjusted Final Score: ±5 points (60% weight only)

**Vote Confidence:** 0.6 (partial verification capability)

### Weighted Consensus Calculation

```cpp
struct ConsensusResult {
    double acceptWeight = 0.0;
    double rejectWeight = 0.0;
    double totalWeight = 0.0;
    
    for (auto& response : validatorResponses) {
        double weight = response.voteConfidence; // 1.0 or 0.6
        totalWeight += weight;
        
        if (response.vote == ACCEPT) {
            acceptWeight += weight;
        } else if (response.vote == REJECT) {
            rejectWeight += weight;
        }
        // ABSTAIN votes don't count toward either
    }
    
    double acceptanceRate = acceptWeight / totalWeight;
    double rejectionRate = rejectWeight / totalWeight;
    
    if (acceptanceRate >= 0.70) {
        return CONSENSUS_REACHED_ACCEPT;
    } else if (rejectionRate >= 0.30) {
        return DISPUTED;
    } else {
        return DISPUTED; // No clear majority
    }
}
```

## Example Scenarios

### Scenario 1: Legitimate WoT Difference

```
Alice claims: Score=85 (WoT=34, Behavior=26, Economic=17, Temporal=8)

Validator 1 (HAS WoT):
  Calculates: WoT=30, Behavior=26, Economic=17, Temporal=8
  WoT diff: 4 points ✓ (within ±5)
  Other components: Perfect match ✓
  Vote: ACCEPT (confidence: 1.0)

Validator 2 (NO WoT):
  WoT: IGNORED
  Calculates: Behavior=26, Economic=17, Temporal=8
  All verifiable components: Perfect match ✓
  Vote: ACCEPT (confidence: 0.6)

Result: Both validators ACCEPT despite WoT difference
```

### Scenario 2: Fraudulent Behavior Score

```
Alice claims: Score=85 (WoT=34, Behavior=26, Economic=17, Temporal=8)
Reality: Alice has poor behavior metrics

Validator 1 (HAS WoT):
  Calculates: WoT=33, Behavior=15, Economic=17, Temporal=8
  Behavior diff: 11 points ✗ (exceeds ±3)
  Vote: REJECT (confidence: 1.0)

Validator 2 (NO WoT):
  WoT: IGNORED
  Calculates: Behavior=15, Economic=17, Temporal=8
  Behavior diff: 11 points ✗ (exceeds ±3)
  Vote: REJECT (confidence: 0.6)

Result: Both validators REJECT - fraud detected
```

### Scenario 3: Mixed Validator Perspectives

```
Alice claims: Score=85 (WoT=34, Behavior=26, Economic=17, Temporal=8)

10 Validators:
- 4 with WoT connection (confidence: 1.0 each)
- 6 without WoT connection (confidence: 0.6 each)

Results:
- 3 WoT validators ACCEPT (WoT within ±5)
- 1 WoT validator REJECT (WoT differs by 15 points)
- 6 non-WoT validators ACCEPT (all non-WoT components match)

Weighted Calculation:
- ACCEPT: (3 × 1.0) + (6 × 0.6) = 3.0 + 3.6 = 6.6
- REJECT: (1 × 1.0) = 1.0
- Total: 7.6
- Acceptance rate: 6.6 / 7.6 = 86.8% → CONSENSUS REACHED
```

## Implementation Requirements

### 1. Update HATv2Score Structure

```cpp
struct HATv2Score {
    uint160 address;
    int16_t finalScore;  // 0-100
    uint64_t timestamp;
    
    // Component breakdown (REQUIRED)
    double behaviorScore;  // 0-30 points (30% weight)
    double wotScore;       // 0-40 points (40% weight)
    double economicScore;  // 0-20 points (20% weight)
    double temporalScore;  // 0-10 points (10% weight)
    
    // Metadata
    bool hasWoTConnection;
    uint32_t wotPathCount;
    double wotPathStrength;
    
    // Validation
    bool IsValid() const {
        // Components must sum to final score (within rounding tolerance)
        double componentSum = behaviorScore + wotScore + economicScore + temporalScore;
        return abs(componentSum - finalScore) <= 2;
    }
};
```

### 2. Update CalculateValidatorVote()

```cpp
ValidationVote CalculateValidatorVote(
    const HATv2Score& selfReported,
    const HATv2Score& calculated,
    bool hasWoT
) {
    if (hasWoT) {
        // Verify all components
        if (abs(selfReported.wotScore - calculated.wotScore) > 5) return REJECT;
        if (abs(selfReported.behaviorScore - calculated.behaviorScore) > 3) return REJECT;
        if (abs(selfReported.economicScore - calculated.economicScore) > 3) return REJECT;
        if (abs(selfReported.temporalScore - calculated.temporalScore) > 3) return REJECT;
        if (abs(selfReported.finalScore - calculated.finalScore) > 8) return REJECT;
        return ACCEPT;
    } else {
        // Verify only non-WoT components
        if (abs(selfReported.behaviorScore - calculated.behaviorScore) > 3) return REJECT;
        if (abs(selfReported.economicScore - calculated.economicScore) > 3) return REJECT;
        if (abs(selfReported.temporalScore - calculated.temporalScore) > 3) return REJECT;
        
        // Calculate adjusted score (60% weight)
        double adjustedSelf = selfReported.behaviorScore + 
                             selfReported.economicScore + 
                             selfReported.temporalScore;
        double adjustedCalc = calculated.behaviorScore + 
                             calculated.economicScore + 
                             calculated.temporalScore;
        
        if (abs(adjustedSelf - adjustedCalc) > 5) return REJECT;
        return ACCEPT;
    }
}
```

### 3. Update Vote Confidence Calculation

```cpp
double CalculateVoteConfidence(
    const uint160& validator,
    const uint160& target
) {
    bool hasWoT = HasWoTConnection(validator, target);
    
    if (hasWoT) {
        return 1.0;  // Full verification capability
    } else {
        // Can only verify 60% of score (non-WoT components)
        return 0.6;  // Partial verification capability
    }
}
```

## Security Benefits

1. **Prevents False Rejections**: Validators without WoT don't penalize for differences they can't verify
2. **Maintains Fraud Detection**: All validators can detect fraudulent behavior/economic/temporal components
3. **Accounts for Personalization**: WoT differences are expected and tolerated appropriately
4. **Weighted Consensus**: Vote confidence reflects actual verification capability
5. **Component Isolation**: Fraud in one component doesn't require rejecting entire score

## Eclipse Attack and Sybil Network Protection

### Attack Scenario

An attacker could:
1. Create 100+ Sybil nodes with mutual WoT connections
2. Isolate the Sybil network from the main network (Eclipse Attack)
3. Maintain only 1 connection to the real network
4. When validator selection occurs, Sybil nodes have high probability of being selected
5. Sybil validators validate each other's fraudulent reputation claims

### Mitigation Strategies

#### 1. Network Topology Diversity Requirement

```cpp
struct ValidatorSelectionConstraints {
    // Require validators from different network regions
    int minNetworkRegions = 3;  // At least 3 different /16 IP subnets
    
    // Require validators with diverse peer connections
    int minUniquePeerOverlap = 5;  // Validators must have <50% peer overlap
    
    // Require validators with different uptime patterns
    int minUptimeVariance = 24;  // Hours of uptime variance
};

bool ValidateNetworkDiversity(const std::vector<uint160>& validators) {
    // Check IP subnet diversity
    std::set<std::string> subnets;
    for (auto& validator : validators) {
        std::string subnet = GetIPSubnet(validator, 16);
        subnets.insert(subnet);
    }
    if (subnets.size() < minNetworkRegions) {
        return false;  // Too concentrated in one network region
    }
    
    // Check peer connection diversity
    for (size_t i = 0; i < validators.size(); i++) {
        for (size_t j = i + 1; j < validators.size(); j++) {
            double overlap = CalculatePeerOverlap(validators[i], validators[j]);
            if (overlap > 0.5) {
                return false;  // Validators share too many peers (likely Sybil)
            }
        }
    }
    
    return true;
}
```

#### 2. Validator Reputation History Requirements

```cpp
struct ValidatorEligibility {
    // Minimum on-chain history
    int minBlocksSinceFirstSeen = 10000;  // ~70 days
    
    // Minimum validation history
    int minPreviousValidations = 50;
    
    // Minimum accuracy score
    double minAccuracyScore = 0.85;  // 85% accurate validations
    
    // Maximum stake from single entity
    double maxStakeConcentration = 0.20;  // No entity controls >20% of validator stake
};

bool IsEligibleValidator(const uint160& validator) {
    // Check on-chain history
    int blocksSinceFirstSeen = GetBlocksSinceFirstSeen(validator);
    if (blocksSinceFirstSeen < minBlocksSinceFirstSeen) {
        return false;  // Too new, likely Sybil
    }
    
    // Check validation history
    ValidatorStats stats = GetValidatorStats(validator);
    if (stats.totalValidations < minPreviousValidations) {
        return false;  // Insufficient validation history
    }
    
    // Check accuracy
    if (stats.accuracyScore < minAccuracyScore) {
        return false;  // Poor validation accuracy
    }
    
    // Check stake concentration (wallet clustering)
    std::vector<uint160> cluster = GetWalletCluster(validator);
    double totalStake = GetTotalValidatorStake();
    double clusterStake = GetClusterStake(cluster);
    if (clusterStake / totalStake > maxStakeConcentration) {
        return false;  // Too much stake concentration
    }
    
    return true;
}
```

#### 3. Cross-Validation with Non-WoT Validators

```cpp
struct ConsensusRequirements {
    // Require minimum non-WoT validators
    int minNonWoTValidators = 4;  // At least 40% without WoT connection
    
    // Require agreement between WoT and non-WoT validators
    double minCrossGroupAgreement = 0.60;  // 60% agreement between groups
};

bool ValidateConsensusIntegrity(const std::vector<ValidationResponse>& responses) {
    // Separate validators by WoT connection
    std::vector<ValidationResponse> wotValidators;
    std::vector<ValidationResponse> nonWotValidators;
    
    for (auto& response : responses) {
        if (response.hasWoTConnection) {
            wotValidators.push_back(response);
        } else {
            nonWotValidators.push_back(response);
        }
    }
    
    // Require minimum non-WoT validators
    if (nonWotValidators.size() < minNonWoTValidators) {
        return false;  // Insufficient non-WoT validators (possible Sybil network)
    }
    
    // Calculate agreement between groups
    double wotAcceptRate = CalculateAcceptRate(wotValidators);
    double nonWotAcceptRate = CalculateAcceptRate(nonWotValidators);
    double agreement = 1.0 - abs(wotAcceptRate - nonWotAcceptRate);
    
    if (agreement < minCrossGroupAgreement) {
        // WoT and non-WoT validators disagree significantly
        // Possible Sybil network or Eclipse attack
        EscalateToDAO("Cross-group validator disagreement");
        return false;
    }
    
    return true;
}
```

#### 4. Wallet Clustering Integration

```cpp
bool DetectSybilValidators(const std::vector<uint160>& validators) {
    // Use wallet clustering to detect Sybil networks
    std::map<uint160, std::vector<uint160>> clusters;
    
    for (auto& validator : validators) {
        std::vector<uint160> cluster = GetWalletCluster(validator);
        clusters[validator] = cluster;
    }
    
    // Check for overlapping clusters
    for (size_t i = 0; i < validators.size(); i++) {
        for (size_t j = i + 1; j < validators.size(); j++) {
            auto& cluster1 = clusters[validators[i]];
            auto& cluster2 = clusters[validators[j]];
            
            // Calculate cluster overlap
            int overlap = CountOverlap(cluster1, cluster2);
            double overlapRatio = (double)overlap / std::min(cluster1.size(), cluster2.size());
            
            if (overlapRatio > 0.3) {
                // Validators likely controlled by same entity
                LogWarning("Potential Sybil validators detected: %s and %s",
                          validators[i].ToString(), validators[j].ToString());
                return true;
            }
        }
    }
    
    return false;
}
```

#### 5. Economic Stake Requirements

```cpp
struct StakeRequirements {
    // Minimum stake per validator
    CAmount minStakePerValidator = 10 * COIN;  // 10 CAS
    
    // Stake must be aged (not freshly acquired)
    int minStakeAge = 1000;  // ~7 days in blocks
    
    // Stake must come from diverse sources
    int minStakeSources = 3;  // At least 3 different funding sources
};

bool ValidateValidatorStake(const uint160& validator) {
    // Check stake amount
    CAmount stake = GetValidatorStake(validator);
    if (stake < minStakePerValidator) {
        return false;
    }
    
    // Check stake age
    int stakeAge = GetStakeAge(validator);
    if (stakeAge < minStakeAge) {
        return false;  // Freshly staked, likely Sybil
    }
    
    // Check stake source diversity
    std::vector<uint160> stakeSources = GetStakeSources(validator);
    if (stakeSources.size() < minStakeSources) {
        return false;  // Stake from too few sources
    }
    
    // Check if stake sources are in same wallet cluster
    for (auto& source : stakeSources) {
        std::vector<uint160> cluster = GetWalletCluster(source);
        if (cluster.size() > stakeSources.size() * 0.5) {
            return false;  // Most stake from same cluster
        }
    }
    
    return true;
}
```

#### 6. Behavioral Analysis

```cpp
struct BehaviorAnalysis {
    // Check for coordinated behavior patterns
    bool DetectCoordinatedBehavior(const std::vector<uint160>& validators) {
        // Check transaction timing correlation
        std::vector<std::vector<int64_t>> txTimestamps;
        for (auto& validator : validators) {
            txTimestamps.push_back(GetTransactionTimestamps(validator));
        }
        
        double correlation = CalculateTimingCorrelation(txTimestamps);
        if (correlation > 0.7) {
            return true;  // Highly correlated timing (likely same operator)
        }
        
        // Check for identical transaction patterns
        for (size_t i = 0; i < validators.size(); i++) {
            for (size_t j = i + 1; j < validators.size(); j++) {
                double similarity = CalculateTransactionPatternSimilarity(
                    validators[i], validators[j]
                );
                if (similarity > 0.8) {
                    return true;  // Very similar patterns (likely Sybil)
                }
            }
        }
        
        return false;
    }
};
```

### Implementation Priority

1. **Phase 1 (Critical)**: Network topology diversity + Wallet clustering integration
2. **Phase 2 (Important)**: Validator reputation history + Economic stake requirements
3. **Phase 3 (Enhanced)**: Cross-validation requirements + Behavioral analysis

### Testing Requirements

1. **Sybil Network Simulation**: Create 100 Sybil nodes and test detection
2. **Eclipse Attack Simulation**: Isolate network and test validator selection
3. **Mixed Attack Simulation**: Combine Sybil + Eclipse + WoT manipulation
4. **Performance Testing**: Ensure detection doesn't slow down validation

## Testing Requirements

### Unit Tests

1. Test component-based verification with WoT validators
2. Test component-based verification without WoT validators
3. Test weighted consensus calculation
4. Test edge cases (missing components, invalid data)

### Integration Tests

1. Test mixed validator scenarios (some with WoT, some without)
2. Test fraud detection across all components
3. Test legitimate WoT differences don't cause rejection
4. Test DAO escalation for disputed cases

### Security Tests

1. Test Sybil attack resistance with component verification
2. Test collusion detection across components
3. Test manipulation attempts on individual components
4. Test validator accountability with component-level tracking

## Migration Path

1. **Phase 1**: Update HATv2Score structure to include component breakdown
2. **Phase 2**: Implement component-based verification logic
3. **Phase 3**: Update consensus calculation with weighted voting
4. **Phase 4**: Deploy and test with real network data
5. **Phase 5**: Monitor and adjust tolerances based on empirical data

## Conclusion

Component-based verification provides a more robust and fair consensus mechanism that:
- Respects the personalized nature of WoT scores
- Prevents false rejections due to legitimate perspective differences
- Maintains strong fraud detection across all verifiable components
- Provides appropriate weighting based on verification capability

This approach is essential for a production-ready HAT v2 consensus system.
