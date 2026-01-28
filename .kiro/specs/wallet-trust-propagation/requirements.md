# Requirements Document

## Introduction

This feature extends the Cascoin Web-of-Trust reputation system to propagate trust relations across all addresses within a wallet cluster. Currently, trust relations are address-based, allowing malicious actors to escape negative reputation by generating new addresses within the same wallet. This feature ensures that trust relations (both positive and negative) apply to the entire wallet, preventing reputation gaming through address rotation.

## Glossary

- **Trust_Propagation_System**: The component responsible for propagating trust relations from individual addresses to all addresses within the same wallet cluster
- **Wallet_Cluster**: A group of addresses identified as belonging to the same wallet through transaction analysis (common inputs, change addresses, behavioral patterns)
- **Trust_Edge**: A directed relationship from one address to another with a weight (-100 to +100) and bonded stake
- **Cluster_Trust_Index**: A database index mapping wallet clusters to their aggregated trust relations
- **Propagated_Trust_Edge**: A trust edge that has been automatically applied to all addresses in a wallet cluster
- **Trust_Inheritance**: The mechanism by which new addresses in a wallet automatically receive existing trust relations
- **Canonical_Address**: The primary address of a wallet cluster used as the reference for cluster-level trust operations

## Requirements

### Requirement 1: Trust Propagation to Existing Wallet Addresses

**User Story:** As a Web-of-Trust participant, I want trust relations I establish with one address to automatically apply to all addresses in that wallet's cluster, so that scammers cannot escape negative reputation by using different addresses from the same wallet.

#### Acceptance Criteria

1. WHEN a trust edge is added to an address THEN THE Trust_Propagation_System SHALL identify the wallet cluster containing that address
2. WHEN a wallet cluster is identified for a trust target THEN THE Trust_Propagation_System SHALL create propagated trust edges to all member addresses in the cluster
3. WHEN propagated trust edges are created THEN THE Trust_Propagation_System SHALL store them with a reference to the original trust edge
4. WHEN querying trust for any address in a cluster THEN THE Trust_Propagation_System SHALL return both direct and propagated trust edges
5. IF the wallet clustering system fails to identify a cluster THEN THE Trust_Propagation_System SHALL apply trust only to the specified address and log a warning

### Requirement 2: Trust Inheritance for New Addresses

**User Story:** As a Web-of-Trust participant, I want new addresses generated in a wallet to automatically inherit existing trust relations, so that scammers cannot escape reputation by creating new addresses.

#### Acceptance Criteria

1. WHEN a new address is detected in an existing wallet cluster THEN THE Trust_Propagation_System SHALL propagate all existing trust edges to the new address
2. WHEN trust edges are propagated to a new address THEN THE Trust_Propagation_System SHALL preserve the original trust weight, bond amount, and timestamp
3. WHEN a new address inherits trust THEN THE Trust_Propagation_System SHALL emit an event for audit purposes
4. WHILE processing new blocks THEN THE Trust_Propagation_System SHALL check for new addresses joining existing clusters
5. IF a new address cannot inherit trust due to database errors THEN THE Trust_Propagation_System SHALL retry up to 3 times before logging an error

### Requirement 3: Cluster-Level Trust Operations via RPC

**User Story:** As a developer, I want RPC commands that operate on wallet clusters rather than individual addresses, so that I can efficiently manage trust relations at the wallet level.

#### Acceptance Criteria

1. WHEN the `addclustertrust` RPC is called with a target address THEN THE Trust_Propagation_System SHALL add trust to all addresses in the target's wallet cluster
2. WHEN the `getclustertrust` RPC is called THEN THE Trust_Propagation_System SHALL return aggregated trust information for the entire wallet cluster
3. WHEN the `listclustertrustrelations` RPC is called THEN THE Trust_Propagation_System SHALL return all trust relations affecting any address in the cluster
4. WHEN cluster-level RPC commands are called THEN THE Trust_Propagation_System SHALL return the cluster ID and member count in the response
5. IF an RPC command targets an address not in any cluster THEN THE Trust_Propagation_System SHALL treat it as a single-address cluster

### Requirement 4: Integration with Existing Trust Commands

**User Story:** As a developer, I want the existing trust commands to automatically use wallet-level trust propagation, so that all trust operations benefit from Sybil resistance.

#### Acceptance Criteria

1. WHEN `addtrust` is called THEN THE Trust_Propagation_System SHALL automatically propagate trust to the entire wallet cluster of the target address
2. WHEN `geteffectivetrust` is called THEN THE Trust_Propagation_System SHALL consider both direct and propagated trust edges in the calculation
3. WHEN `sendtrustrelation` is called THEN THE Trust_Propagation_System SHALL broadcast trust for the wallet cluster
4. WHEN `getweightedreputation` is called THEN THE Trust_Propagation_System SHALL aggregate reputation across the wallet cluster
5. THE Trust_Propagation_System SHALL update the existing RPC command documentation to reflect cluster-aware behavior

### Requirement 5: Trust Propagation Database Storage

**User Story:** As a system operator, I want trust propagation data to be efficiently stored and indexed, so that queries remain performant even with large wallet clusters.

#### Acceptance Criteria

1. THE Trust_Propagation_System SHALL store propagated trust edges with a distinct key prefix (`trust_prop_`)
2. THE Trust_Propagation_System SHALL maintain a cluster-to-trust index for efficient lookups
3. WHEN a trust edge is deleted or modified THEN THE Trust_Propagation_System SHALL update all propagated edges accordingly
4. THE Trust_Propagation_System SHALL support querying all propagated edges for a given original trust edge
5. WHEN serializing propagated trust edges THEN THE Trust_Propagation_System SHALL include a reference to the source edge transaction hash

### Requirement 6: Cluster Update Handling

**User Story:** As a system operator, I want the trust propagation system to handle wallet cluster updates correctly, so that trust relations remain accurate as cluster membership changes.

#### Acceptance Criteria

1. WHEN two clusters merge (addresses found to belong to same wallet) THEN THE Trust_Propagation_System SHALL combine their trust relations
2. WHEN a cluster merge occurs THEN THE Trust_Propagation_System SHALL propagate trust from both original clusters to all merged addresses
3. WHEN cluster membership changes THEN THE Trust_Propagation_System SHALL emit events for monitoring
4. IF conflicting trust edges exist after a merge THEN THE Trust_Propagation_System SHALL use the most recent edge as authoritative
5. WHEN processing cluster updates THEN THE Trust_Propagation_System SHALL maintain atomicity to prevent inconsistent states

### Requirement 7: Performance and Scalability

**User Story:** As a node operator, I want trust propagation to be efficient, so that it does not significantly impact node performance.

#### Acceptance Criteria

1. WHEN propagating trust to a cluster THEN THE Trust_Propagation_System SHALL complete within O(n) time where n is cluster size
2. THE Trust_Propagation_System SHALL limit cluster size processing to a maximum of 10,000 addresses per operation
3. WHEN processing large clusters THEN THE Trust_Propagation_System SHALL use batch database operations
4. THE Trust_Propagation_System SHALL cache cluster membership for frequently accessed addresses
5. WHEN cache size exceeds 100MB THEN THE Trust_Propagation_System SHALL evict least-recently-used entries
