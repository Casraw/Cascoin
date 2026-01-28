# Implementation Plan: Wallet Trust Propagation

## Overview

This implementation plan breaks down the wallet trust propagation feature into discrete coding tasks. The feature extends Cascoin's Web-of-Trust to propagate trust relations across wallet clusters, preventing reputation gaming through address rotation.

## Tasks

- [x] 1. Create core data structures and interfaces
  - [x] 1.1 Create `src/cvm/trustpropagator.h` with PropagatedTrustEdge and ClusterTrustSummary structs
    - Define PropagatedTrustEdge with fromAddress, toAddress, originalTarget, sourceEdgeTx, trustWeight, propagatedAt, bondAmount
    - Define ClusterTrustSummary with clusterId, memberAddresses, scores, edgeCount
    - Add serialization methods for LevelDB storage
    - _Requirements: 5.1, 5.5_
  
  - [x] 1.2 Write property test for PropagatedTrustEdge serialization round-trip
    - **Property 2: Propagated Edge Source Traceability**
    - **Validates: Requirements 1.3, 5.5**

- [x] 2. Implement TrustPropagator class
  - [x] 2.1 Create `src/cvm/trustpropagator.cpp` with constructor and basic methods
    - Implement constructor taking CVMDatabase&, WalletClusterer&, TrustGraph&
    - Implement StorePropagatedEdge() for database writes with `trust_prop_` prefix
    - Implement IndexPropagatedEdge() for source-to-propagated index
    - _Requirements: 5.1, 5.2_
  
  - [x] 2.2 Implement PropagateTrustEdge() method
    - Get cluster members from WalletClusterer
    - Create PropagatedTrustEdge for each member
    - Store edges and update index
    - Return count of propagated edges
    - _Requirements: 1.1, 1.2, 1.3_
  
  - [x] 2.3 Write property test for trust propagation completeness
    - **Property 1: Trust Propagation Completeness**
    - **Validates: Requirements 1.2, 3.1, 4.1**
  
  - [x] 2.4 Implement GetPropagatedEdgesForAddress() method
    - Query database with `trust_prop_` prefix for target address
    - Deserialize and return vector of PropagatedTrustEdge
    - _Requirements: 1.4_
  
  - [x] 2.5 Write property test for query completeness
    - **Property 3: Query Completeness**
    - **Validates: Requirements 1.4**
  
  - [x] 2.6 Implement DeletePropagatedEdges() and UpdatePropagatedEdges() methods
    - Query index by sourceEdgeTx to find all propagated edges
    - Delete or update each propagated edge
    - Update index accordingly
    - _Requirements: 5.3_
  
  - [x] 2.7 Write property test for cascade update propagation
    - **Property 12: Cascade Update Propagation**
    - **Validates: Requirements 5.3**

- [x] 3. Checkpoint - Verify core propagation works
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement trust inheritance for new cluster members
  - [x] 4.1 Implement InheritTrustForNewMember() in TrustPropagator
    - Get all trust edges targeting existing cluster members
    - Create propagated edges for the new address
    - Preserve original weight, bond, timestamp
    - _Requirements: 2.1, 2.2_
  
  - [x] 4.2 Write property test for new member trust inheritance
    - **Property 4: New Member Trust Inheritance**
    - **Validates: Requirements 2.1**
  
  - [x] 4.3 Write property test for propagated edge data integrity
    - **Property 5: Propagated Edge Data Integrity**
    - **Validates: Requirements 2.2**

- [x] 5. Implement ClusterUpdateHandler for block processing
  - [x] 5.1 Create `src/cvm/clusterupdatehandler.h` with ClusterUpdateEvent struct
    - Define event types: NEW_MEMBER, CLUSTER_MERGE, TRUST_INHERITED
    - Define ClusterUpdateHandler class interface
    - _Requirements: 2.3, 6.3_
  
  - [x] 5.2 Create `src/cvm/clusterupdatehandler.cpp` with ProcessBlock() implementation
    - Detect new cluster members from transaction inputs
    - Call InheritTrustForNewMember() for each new member
    - Emit ClusterUpdateEvent for each change
    - _Requirements: 2.3, 2.4_
  
  - [x] 5.3 Write property test for cluster update event emission
    - **Property 6: Cluster Update Event Emission**
    - **Validates: Requirements 2.3, 6.3**

- [x] 6. Implement ClusterTrustQuery for cluster-aware queries
  - [x] 6.1 Create `src/cvm/clustertrustquery.h` with class interface
    - Define GetEffectiveTrust(), GetAllClusterTrustEdges(), GetClusterIncomingTrust()
    - _Requirements: 4.2, 4.4_
  
  - [x] 6.2 Implement GetEffectiveTrust() with minimum scoring
    - Get all cluster members
    - Calculate trust score for each member
    - Return minimum score across cluster
    - _Requirements: 4.2_
  
  - [x] 6.3 Write property test for cluster-aware minimum scoring
    - **Property 9: Cluster-Aware Minimum Scoring**
    - **Validates: Requirements 4.2, 4.4**
  
  - [x] 6.4 Implement GetAllClusterTrustEdges() for complete listing
    - Collect direct edges for all cluster members
    - Collect propagated edges for all cluster members
    - Deduplicate and return combined list
    - _Requirements: 3.3_
  
  - [x] 6.5 Write property test for trust relation listing completeness
    - **Property 8: Trust Relation Listing Completeness**
    - **Validates: Requirements 3.3**

- [x] 7. Checkpoint - Verify query and inheritance logic
  - Ensure all tests pass, ask the user if questions arise.

- [x] 8. Implement cluster merge handling
  - [x] 8.1 Implement HandleClusterMerge() in TrustPropagator
    - Get trust edges from both original clusters
    - Propagate all edges to all addresses in merged cluster
    - Handle conflicts by using most recent timestamp
    - _Requirements: 6.1, 6.2, 6.4_
  
  - [x] 8.2 Write property test for cluster merge trust combination
    - **Property 13: Cluster Merge Trust Combination**
    - **Validates: Requirements 6.1, 6.2**
  
  - [x] 8.3 Write property test for conflict resolution by timestamp
    - **Property 14: Conflict Resolution by Timestamp**
    - **Validates: Requirements 6.4**

- [x] 9. Implement GetClusterTrustSummary() method
  - [x] 9.1 Add GetClusterTrustSummary() to TrustPropagator
    - Aggregate trust information for entire cluster
    - Calculate effective score, edge counts
    - Cache results for performance
    - _Requirements: 3.2_

- [x] 10. Implement RPC commands
  - [x] 10.1 Add `addclustertrust` RPC command in `src/rpc/cvm.cpp`
    - Parse address, weight, bond, reason parameters
    - Get cluster for target address
    - Call PropagateTrustEdge() for the cluster
    - Return cluster_id, members_affected, edges_created
    - _Requirements: 3.1, 3.4_
  
  - [x] 10.2 Add `getclustertrust` RPC command
    - Parse address and optional viewer parameters
    - Call GetClusterTrustSummary()
    - Return cluster_id, member_count, members, effective_score, worst_member
    - _Requirements: 3.2, 3.4_
  
  - [x] 10.3 Add `listclustertrustrelations` RPC command
    - Parse address and optional max_count parameters
    - Call GetAllClusterTrustEdges()
    - Return cluster_id, direct_edges, propagated_edges, total_count
    - _Requirements: 3.3, 3.4_
  
  - [x] 10.4 Write property test for RPC response format consistency
    - **Property 7: RPC Response Format Consistency**
    - **Validates: Requirements 3.2, 3.4**

- [x] 11. Replace existing trust commands with cluster-aware versions
  - [x] 11.1 Modify `addtrust` RPC to always propagate to wallet cluster
    - Get wallet cluster for target address
    - Create trust edge to canonical cluster address
    - Call propagator.PropagateTrustEdge() to all cluster members
    - All future addresses in wallet will inherit via ClusterUpdateHandler
    - _Requirements: 4.1_
  
  - [x] 11.2 Modify `geteffectivetrust` RPC to use ClusterTrustQuery
    - Replace direct score calculation with ClusterTrustQuery.GetEffectiveTrust()
    - Returns minimum score across ALL addresses in wallet cluster
    - Add cluster information to response
    - _Requirements: 4.2_
  
  - [x] 11.3 Modify `sendtrustrelation` RPC to propagate on-chain
    - After broadcasting trust transaction, propagate to entire wallet cluster
    - _Requirements: 4.3_
  
  - [x] 11.4 Modify `getweightedreputation` RPC to aggregate across cluster
    - Use ClusterTrustQuery for cluster-aware reputation
    - New addresses in wallet automatically included
    - _Requirements: 4.4_

- [x] 12. Checkpoint - Verify RPC integration
  - Ensure all tests pass, ask the user if questions arise.

- [x] 13. Implement performance optimizations
  - [x] 13.1 Add cluster size limit enforcement
    - Check cluster size before propagation
    - If > 10,000, process in batches or return limited flag
    - _Requirements: 7.2_
  
  - [x] 13.2 Write property test for cluster size limit enforcement
    - **Property 15: Cluster Size Limit Enforcement**
    - **Validates: Requirements 7.2**
  
  - [x] 13.3 Add caching for cluster trust summaries
    - Implement LRU cache with 100MB limit
    - Invalidate cache on cluster updates
    - _Requirements: 7.4, 7.5_

- [x] 14. Implement index round-trip verification
  - [x] 14.1 Write property test for index round-trip consistency
    - **Property 11: Index Round-Trip Consistency**
    - **Validates: Requirements 5.2, 5.4**

- [x] 15. Implement storage key prefix verification
  - [x] 15.1 Write property test for storage key prefix convention
    - **Property 10: Storage Key Prefix Convention**
    - **Validates: Requirements 5.1**

- [x] 16. Wire components into block processing
  - [x] 16.1 Integrate ClusterUpdateHandler into BlockProcessor
    - Add ClusterUpdateHandler member to BlockProcessor
    - Call ProcessBlock() after processing CVM transactions
    - _Requirements: 2.4_
  
  - [x] 16.2 Register new RPC commands in RPC table
    - Add entries for addclustertrust, getclustertrust, listclustertrustrelations
    - _Requirements: 3.1, 3.2, 3.3_

- [x] 17. Update Makefile.am for new source files
  - [x] 17.1 Add new source files to `src/Makefile.am`
    - Add trustpropagator.cpp/h
    - Add clusterupdatehandler.cpp/h
    - Add clustertrustquery.cpp/h
    - Add test files to test sources

- [~] 18. Final checkpoint - Full integration test
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- All tasks including property-based tests are required for comprehensive testing
- Each task references specific requirements for traceability
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases
- The implementation builds incrementally, with checkpoints to verify progress
