**Subject: Cascoin (CAS) – Project Breakdown & Integration Details**

Hi team,

thanks for getting back to me. Please find a full breakdown of the Cascoin project below, including the technical details relevant for integration.

Cascoin today is a plain, well-behaved UTXO chain, and integrating it is a standard Bitcoin/Litecoin-style job. In parallel I am building a second layer of features in the `cvm` branch, which currently runs on our public testnet: an EVM-compatible smart contract VM, a Layer 2 rollup, and a Web-of-Trust reputation system aimed at making on-chain scams much harder to pull off. None of that is live on mainnet yet, and none of it changes anything about how you would integrate CAS now. I have described it in the "Roadmap" section further down, because I think it is the part that makes Cascoin worth listing rather than just another PoW fork.

A quick note on the project: Cascoin is an independent, self-funded project maintained by me (Alexander Bergmann, aka Casraw), with a community of contributors and miners around it.

## Project Overview

Cascoin (CAS) is a UTXO-based cryptocurrency forked from Litecoin Cash (which itself derives from the Litecoin/Bitcoin Core codebase, C++), with native SegWit support. Its distinguishing feature is a hybrid consensus model that combines two Proof-of-Work algorithms with a mechanism called Labyrinth Mining:

- **Dual PoW** – SHA256 (for ASIC miners) and MinotaurX (CPU-friendly, ASIC-resistant), so both hardware types can participate.
- **Labyrinth Mining** – An evolution of the Hive Mining concept: users create on-chain "mice" that can mine blocks without dedicated hardware, alongside standard PoW blocks.

The chain launched with a fair start on 13 April 2025. There was **no premine and no ICO** (premine amount = 0).

## Key Specifications

| Parameter | Value |
|-----------|-------|
| Coin name / Ticker | Cascoin / CAS |
| Codebase | Litecoin Cash fork (C++), SegWit enabled |
| Consensus | Hybrid: Dual PoW (SHA256 + MinotaurX) + Labyrinth Mining |
| Block time | 2.5 minutes |
| Max supply | 84,000,000 CAS |
| Halving interval | 840,000 blocks |
| PoW block reward | 25 CAS |
| Labyrinth block reward | 75 CAS |
| Premine | None |
| Decimals | 7 (base unit = 10,000,000; due to 10:1 coin scale) |
| P2P port | 22222 (mainnet) / 22223 (testnet) |
| RPC | Bitcoin-compatible JSON-RPC |
| License | MIT |

## Address Formats

The QT wallet generates the following address types. Standard/default receiving addresses are the native SegWit (`cas1...`) and P2SH (`M...`) formats; legacy addresses are also supported:

| Type | Prefix | Notes |
|------|--------|-------|
| Native SegWit (bech32) | `cas1...` | Default, recommended |
| P2SH | `M...` | Standard, wallet-generated |
| Legacy P2PKH | `H...` | Legacy support |

## Genesis Block

- Hash: `00000928be1f2ccc448590307e4f6e165702244b5be0f79c08e48d1fc7128c82`
- Launch date: 13 April 2025

## Integration Notes

- The daemon exposes a standard Bitcoin/Litecoin-style JSON-RPC interface, so existing Bitcoin/Litecoin integrations can be reused with minimal changes.
- Please note: CAS uses **7 decimal places** (1 CAS = 10,000,000 base units), not the usual 8. This is important for correct balance/amount handling in your integration.
- Address formats: native SegWit (`cas1...`), P2SH (`M...`) and legacy P2PKH (`H...`) are all supported. I recommend supporting `cas1...` and `M...` as the primary deposit/withdrawal formats.
- Prebuilt binaries for Linux, Windows and macOS are provided, and full build instructions are in the repo.
- I recommend a confirmation threshold of around 20 confirmations (~50 minutes) for deposit crediting, and I'm happy to align this with your risk policy.

## Roadmap: CVM, Web-of-Trust and Layer 2

Everything in this section lives in the `cvm` branch and runs on the Cascoin **testnet** today. It is **not active on mainnet** and is not required for a listing. I am including it because it is where the project is heading, and because it is the reason I think Cascoin is worth a slot on SafeTrade rather than just another SHA256 fork.

Two design principles run through all of it:

1. **Soft fork, never a chain split.** Every new feature is carried in `OP_RETURN` outputs. Old nodes see valid, unspendable outputs and keep following the chain; upgraded nodes parse and act on the payload. No hard fork, no coordinated flag day for wallets or exchanges.
2. **Activation is height-gated per network.** Each feature has an explicit activation height in `chainparams`, so the whole network switches on the same rules at the same block. Testnet is past all of them. On mainnet the heights are set but the `cvm` branch is not yet released, so nothing is running there — and the L2 and post-quantum heights are deliberately placed well ahead of the current tip as coordinated future upgrades. Nothing turns on by surprise.

### CVM – the Cascoin Virtual Machine

The CVM is a gas-metered smart contract engine built into the node, with its own LevelDB state store separate from the UTXO set.

| Property | Value |
|----------|-------|
| Architecture | Register-based VM with an operand stack |
| Max contract size | 24 KB |
| Max gas per transaction | 1,000,000 |
| Max gas per block | 10,000,000 |
| Deployment | `OP_RETURN` payload (soft fork) |
| State | Dedicated LevelDB, separate from the UTXO database |
| Native instruction set | 40+ opcodes: arithmetic, logic, comparison, control flow, persistent storage, crypto (SHA256, signature verification), and block/tx context |

### EVM compatibility

This is the part I am most happy about. The node embeds **evmone via the EVMC interface**, so the CVM does not merely imitate the EVM, it executes real EVM bytecode with mainnet-identical opcode semantics. A bytecode detector inspects incoming code and routes it either to the native CVM interpreter or to the EVM engine, so both worlds coexist on the same chain.

What is in place today:

- Real EVM bytecode execution through evmone (all standard opcodes)
- Keccak-256, RLP encoding, 20-byte addresses, ABI-level calldata
- `CREATE` and `CREATE2` contract address derivation, identical to Ethereum
- Ethereum-style transaction receipts including the 2048-bit log bloom filter
- A JSON-RPC surface under both `cas_*` (canonical) and `eth_*` (alias) names: `eth_call`, `eth_estimateGas`, `eth_getCode`, `eth_getStorageAt`, `eth_sendTransaction`, `eth_getTransactionReceipt`, `eth_blockNumber`, `eth_getBalance`, `eth_getTransactionCount`, `eth_gasPrice`
- Anvil/Hardhat-style development helpers: `evm_snapshot`, `evm_revert`, `evm_mine`, `evm_increaseTime`, plus `debug_traceTransaction` / `debug_traceCall`

Being straight with you about the gaps: the `eth_*` namespace is still **partial**. `eth_sendRawTransaction`, `eth_chainId`, `eth_getLogs` and the block/filter methods are not implemented yet, so this is not a drop-in MetaMask/Hardhat provider today. Completing that namespace is the next milestone. Practically, existing Solidity contracts compile and run, but the tooling story is not finished.

One deliberate deviation from Ethereum: **gas pricing is not identical**. Cascoin layers reputation-aware gas discounts, free-gas allowances and gas subsidy pools on top of the standard cost model, so a well-reputed address pays less for the same computation. Opcode semantics match Ethereum; the bill does not.

### Layer 2

The L2 is an optimistic, rollup-style layer that runs **inside the same `cascoind` process** — there is no separate chain, no separate daemon, no separate token, and no external bridge operator. All L1 interaction happens through `OP_RETURN` markers on the Cascoin chain:

| Marker | Purpose |
|--------|---------|
| `L2BURN` | User burns CAS on L1; the burn is an objective on-chain fact, so after a fixed confirmation depth the L2 deterministically mints the same amount 1:1 |
| `L2COMMIT` | The sequencer anchors the L2 state root (a Sparse Merkle Tree root) to L1 |
| `L2DATA` | The **full serialised bytes of every L2 block** are posted to L1 in chunks, so any node can reconstruct and re-execute the entire L2 from L1 alone |
| `L2SEQREG` | On-chain sequencer registry; the stake is CAS burned into the registration output, giving Sybil resistance |
| `L2FORCE` | Forced inclusion — a user can post a signed L2 transaction directly to L1, so the sequencer cannot censor them |

The `L2DATA` design is the point I would highlight: there is **no data availability trust assumption**. Everything needed to independently verify the L2 is on L1. On top of that sit fraud proofs — every committed state root opens a challenge window, and a challenger who re-executes and finds a mismatch can prove it and get the sequencer permanently excluded.

Current honest status of the L2, so there are no surprises later:

- **Single sequencer.** Decentralised sequencing is designed but not deployed.
- **Transfers only.** L2 executes native value transfers today; smart contracts run on L1, not yet on L2.
- **The peg is one-way.** Burning CAS on L1 destroys that L1 supply permanently and credits an L2 balance. There is currently **no L2 → L1 exit path** implemented. This matters for your supply accounting, and I would not want you to discover it from the code. A trust-minimised exit is on the roadmap; until then the L2 is a testnet-stage system and mainnet activation is set to a future coordinated height.
- Mainnet activation height is defined but deliberately far ahead of the current tip. Testnet has been running it since block 500.

### Web-of-Trust – reputation without a global score

This is the anti-scam system I mentioned at the top, and it is the most unusual thing about Cascoin.

**There is no overall reputation score. There deliberately never will be one.** No canonical number is published for an address, and no authority decides who is trustworthy. Instead, every participant computes reputation *from their own vantage point*:

```
score(viewer -> target) = Σ_X [ opinion(X -> target) × confidence(viewer -> X) ]
                          / Σ_X [ confidence(viewer -> X) ]
```

- `X` ranges over everyone who has expressed a direct opinion about the target.
- `confidence(viewer -> X)` is the strength of the strongest **positive** trust path from the viewer to `X`, computed as the product of the normalised per-hop weights. Trust decays multiplicatively: three hops of 80% trust yield 0.512 confidence.
- Trust is **never routed through someone you distrust**. Weak or negative edges are not traversed, so an attacker cannot inject opinions into your view by building a graph you have no positive path into.
- A trusted intermediary's *negative* opinion actively lowers your view of the target. Distrust propagates, not just trust.

The consequences are the interesting part. Two users can look at the same address and legitimately get different scores, and both are correct — each is the answer to "given who *I* trust, how much should *I* trust this address?" There is nothing for a scammer to farm, because there is no single number to farm. Buying 500 sockpuppet endorsements is worthless against a viewer who has no positive path to any of them. Where no trust path exists at all, the system falls back to the plain crowd average, so a brand-new user still sees something useful.

**Economic security (bond-and-slash).** Trust edges and reputation votes must be bonded with real CAS, sized to the strength of the claim (roughly 1 CAS base plus 0.01 CAS per vote point, so a maximum-strength ±100 vote costs 2 CAS). Slashed bonds and slashed votes stop counting toward every reputation calculation on the network. Spamming opinions is expensive; spamming *dishonest* opinions is expensive and forfeitable.

**DAO arbitration.** A bonded vote can be challenged. DAO members (gated on their own reputation, bonded stake and recent activity) arbitrate with stake-weighted voting, using a commit-reveal scheme so nobody can see the tally before committing. A successful challenge splits the slashed bond 50% to the challenger / 30% to the DAO voters on the winning side / 20% burned. A failed challenge pays 70% of the challenger's forfeited bond to the wrongly accused party and burns the rest. Both sides have skin in the game.

**Sybil resistance.** Two independent mechanisms. Graph analysis flags tight rings where nearly every edge is reciprocated (the classic sockpuppet signature). Separately, on-chain clustering groups addresses that are provably controlled by the same entity via common-input and change-address heuristics, and assigns the cluster the **minimum** reputation of its members. Moving to a fresh address does not shake off a bad record.

### HAT v2 – Hybrid Adaptive Trust

HAT v2 is the composite 0–100 score that pulls the whole picture together:

| Component | Weight | What it measures |
|-----------|--------|------------------|
| Behaviour | 40% | Trade success rate, counterparty diversity, log-scaled volume, bot-pattern detection, fraud history |
| Web-of-Trust | 30% | The personalised score above — which is why **HAT v2 is also observer-dependent** |
| Economic | 20% | Log-scaled staked amount, weighted by how long it has been locked |
| Temporal | 10% | Account age, activity continuity, penalties for suspicious dormancy gaps |

Each component is built multiplicatively from penalty factors, which is what makes it hard to game: pumping one dimension does not help if another collapses. Wash trading fails the diversity test, volume pumping is flattened by the logarithm, and bot-regular intervals fail the coefficient-of-variation check.

### Post-quantum cryptography

Also in the branch: FALCON-512 post-quantum signature support, including a `VERIFY_SIG_QUANTUM` opcode in the CVM, gated behind its own activation height and a version-bits deployment. It is early, but the plumbing is in place rather than deferred indefinitely.

### What this means for SafeTrade

The short version: **nothing you need to do differently today.**

- Reputation and HAT scores are **purely advisory**. They are computed locally by each node from chain data, after a block has already been validated. They can never reject a transaction or a block, and they do not sit anywhere in the consensus path. Listing CAS does not expose you to reputation-based censorship of deposits or withdrawals, by design.
- Everything is soft-fork and height-gated, so an exchange node keeps working across every activation without an emergency upgrade window.
- The standard `getblock` / `sendrawtransaction` / wallet RPC surface you integrate against is unchanged and will stay unchanged.
- When the L2 does activate on mainnet, the one thing that will matter to you is supply accounting for `L2BURN` outputs. I will give you advance notice and documentation well before that height, and I am happy to walk your engineers through it.

I would rather you have the caveats up front than find them yourself. If it is useful, I can give you access to the testnet, a node running the `cvm` branch, and the full technical documentation in the repository.

## Resources

- Website: https://cascoin.net
- Block Explorer: https://casplorer.com
- GitHub: https://github.com/Casraw/Cascoin
- GitHub (`cvm` branch – CVM/EVM, L2, Web-of-Trust): https://github.com/Casraw/Cascoin/tree/cvm
- Whitepaper: `CASCOIN_WHITEPAPER.md` in the repository
- Web-of-Trust documentation: `WEB_OF_TRUST.md` in the repository
- Mining Pool: https://zpool.ca/

## Socials

- Discord (primary community): https://discord.gg/J2NxATBS8z
- BitcoinTalk: https://bitcointalk.org/index.php?topic=5544330
- There is a X account too. But i dont really use it.

I'm happy to provide, testnet coins, brand assets/logo, or any additional documentation you need for the listing and integration. Just let me know.

I hope this summary will help in your decision making.

Best regards,
Alexander Bergmann (Casraw)
Cascoin
