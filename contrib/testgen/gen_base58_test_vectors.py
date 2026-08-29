#!/usr/bin/env python3
# Copyright (c) 2012-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
'''
Generate valid and invalid base58 address and private key test vectors.

Usage: 
    python3 gen_base58_test_vectors.py valid 50 > ../../src/test/data/base58_keys_valid.json
    python3 gen_base58_test_vectors.py invalid 50 > ../../src/test/data/base58_keys_invalid.json
'''
import os
from itertools import islice
from base58 import b58encode_chk, b58decode_chk, b58chars
import random

# Key types - MUST match chainparams.cpp exactly
# Mainnet (from CMainParams)
PUBKEY_ADDRESS_MAIN = 40       # P2PKH addresses start with 'H'
SCRIPT_ADDRESS_MAIN = 8        # P2SH addresses start with '4'
SCRIPT_ADDRESS2_MAIN = 50      # P2SH2 addresses start with 'M'
PRIVKEY_MAIN = 188             # WIF private keys

# Testnet (from CTestNetParams)
PUBKEY_ADDRESS_TEST = 111      # P2PKH addresses start with 'm' or 'n'
SCRIPT_ADDRESS_TEST = 196      # P2SH addresses start with '2'
SCRIPT_ADDRESS2_TEST = 58      # P2SH2 addresses start with 'Q'
PRIVKEY_TEST = 239             # WIF private keys start with 'c' or '9'

# Regtest uses same prefixes as testnet

# Templates: (prefix_bytes, payload_size, suffix_bytes, chain, isPrivkey, isCompressed)
templates = [
    # Mainnet P2PKH
    ((PUBKEY_ADDRESS_MAIN,), 20, (), 'main', False, None),
    # Mainnet P2SH (using SCRIPT_ADDRESS2 which is more commonly used)
    ((SCRIPT_ADDRESS2_MAIN,), 20, (), 'main', False, None),
    # Testnet P2PKH
    ((PUBKEY_ADDRESS_TEST,), 20, (), 'test', False, None),
    # Testnet P2SH (using SCRIPT_ADDRESS2 which is what the encoder uses)
    ((SCRIPT_ADDRESS2_TEST,), 20, (), 'test', False, None),
    # Regtest P2PKH (same as testnet)
    ((PUBKEY_ADDRESS_TEST,), 20, (), 'regtest', False, None),
    # Regtest P2SH (using SCRIPT_ADDRESS2)
    ((SCRIPT_ADDRESS2_TEST,), 20, (), 'regtest', False, None),
    # Mainnet private keys (uncompressed)
    ((PRIVKEY_MAIN,), 32, (), 'main', True, False),
    # Mainnet private keys (compressed)
    ((PRIVKEY_MAIN,), 32, (1,), 'main', True, True),
    # Testnet private keys (uncompressed)
    ((PRIVKEY_TEST,), 32, (), 'test', True, False),
    # Testnet private keys (compressed)
    ((PRIVKEY_TEST,), 32, (1,), 'test', True, True),
    # Regtest private keys (same as testnet)
    ((PRIVKEY_TEST,), 32, (), 'regtest', True, False),
    ((PRIVKEY_TEST,), 32, (1,), 'regtest', True, True),
]

def make_script(prefix_byte, payload_hex):
    """Create the expected script from address prefix and payload."""
    # P2PKH prefixes
    if prefix_byte in (PUBKEY_ADDRESS_MAIN, PUBKEY_ADDRESS_TEST):
        # OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
        return '76a914' + payload_hex + '88ac'
    # P2SH prefixes
    elif prefix_byte in (SCRIPT_ADDRESS_MAIN, SCRIPT_ADDRESS2_MAIN, 
                         SCRIPT_ADDRESS_TEST, SCRIPT_ADDRESS2_TEST):
        # OP_HASH160 <20 bytes> OP_EQUAL
        return 'a914' + payload_hex + '87'
    return payload_hex

def is_valid(v):
    '''Check vector v for validity'''
    result = b58decode_chk(v)
    if result is None:
        return False
    for template in templates:
        prefix = bytes(template[0])
        suffix = bytes(template[2])
        if result.startswith(prefix) and result.endswith(suffix):
            if (len(result) - len(prefix) - len(suffix)) == template[1]:
                return True
    return False

def gen_valid_vectors():
    '''Generate valid test vectors'''
    while True:
        for template in templates:
            prefix = bytes(template[0])
            payload = os.urandom(template[1])
            suffix = bytes(template[2])
            rv = b58encode_chk(prefix + payload + suffix)
            assert is_valid(rv), f"Generated invalid vector: {rv}"
            
            chain = template[3]
            is_privkey = template[4]
            is_compressed = template[5]
            
            metadata = {'chain': chain, 'isPrivkey': is_privkey}
            
            if is_privkey:
                metadata['isCompressed'] = is_compressed
                yield (rv, payload.hex(), metadata)
            else:
                script_hex = make_script(template[0][0], payload.hex())
                yield (rv, script_hex, metadata)

def gen_invalid_vector(template, corrupt_prefix, randomize_payload_size, corrupt_suffix):
    '''Generate possibly invalid vector'''
    if corrupt_prefix:
        prefix = os.urandom(1)
    else:
        prefix = bytes(template[0])
    
    if randomize_payload_size:
        payload = os.urandom(max(int(random.expovariate(0.5)), 50))
    else:
        payload = os.urandom(template[1])
    
    if corrupt_suffix:
        suffix = os.urandom(len(template[2]))
    else:
        suffix = bytes(template[2])

    return b58encode_chk(prefix + payload + suffix)

def randbool(p=0.5):
    '''Return True with P(p)'''
    return random.random() < p

def gen_invalid_vectors():
    '''Generate invalid test vectors'''
    yield ("",)
    yield ("x",)
    while True:
        for template in templates:
            val = gen_invalid_vector(template, randbool(0.2), randbool(0.2), randbool(0.2))
            if random.randint(0, 10) < 1:
                if randbool():
                    val += random.choice(b58chars)
                else:
                    n = random.randint(0, len(val))
                    val = val[0:n] + random.choice(b58chars) + val[n+1:]
            if not is_valid(val):
                yield (val,)

if __name__ == '__main__':
    import sys
    import json
    iters = {'valid': gen_valid_vectors, 'invalid': gen_invalid_vectors}
    try:
        uiter = iters[sys.argv[1]]
    except (IndexError, KeyError):
        uiter = gen_valid_vectors
    try:
        count = int(sys.argv[2])
    except IndexError:
        count = 0
   
    data = list(islice(uiter(), count))
    json.dump(data, sys.stdout, sort_keys=True, indent=4)
    sys.stdout.write('\n')
