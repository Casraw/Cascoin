# Copyright (c) 2012-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
'''
Bitcoin base58 encoding and decoding.

Based on https://bitcointalk.org/index.php?topic=1026.0 (public domain)
Updated for Python 3 compatibility.
'''
import hashlib

__b58chars = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
__b58base = len(__b58chars)
b58chars = __b58chars

def b58encode(v):
    """Encode v, which is a bytes object, to base58 string."""
    if isinstance(v, str):
        v = v.encode('latin-1')
    
    # Convert bytes to integer
    long_value = int.from_bytes(v, 'big')
    
    result = ''
    while long_value >= __b58base:
        div, mod = divmod(long_value, __b58base)
        result = __b58chars[mod] + result
        long_value = div
    result = __b58chars[long_value] + result

    # Bitcoin does a little leading-zero-compression:
    # leading 0-bytes in the input become leading-1s
    nPad = 0
    for c in v:
        if c == 0:
            nPad += 1
        else:
            break

    return (__b58chars[0] * nPad) + result

def b58decode(v, length=None):
    """Decode v (base58 string) into bytes."""
    long_value = 0
    for c in v:
        long_value = long_value * __b58base + __b58chars.find(c)

    # Convert integer to bytes
    result = bytearray()
    while long_value > 0:
        result.insert(0, long_value & 0xff)
        long_value >>= 8

    # Handle leading zeros (1s in base58)
    nPad = 0
    for c in v:
        if c == __b58chars[0]:
            nPad += 1
        else:
            break

    result = bytearray(nPad) + result
    
    if length is not None and len(result) != length:
        return None

    return bytes(result)

def checksum(v):
    """Return 32-bit checksum based on SHA256."""
    if isinstance(v, str):
        v = v.encode('latin-1')
    return hashlib.sha256(hashlib.sha256(v).digest()).digest()[0:4]

def b58encode_chk(v):
    """b58encode a bytes object, with 32-bit checksum."""
    if isinstance(v, str):
        v = v.encode('latin-1')
    return b58encode(v + checksum(v))

def b58decode_chk(v):
    """Decode a base58 string, check and remove checksum."""
    result = b58decode(v)
    if result is None:
        return None
    if result[-4:] == checksum(result[:-4]):
        return result[:-4]
    else:
        return None

def get_bcaddress_version(strAddress):
    """Returns None if strAddress is invalid. Otherwise returns integer version of address."""
    addr = b58decode_chk(strAddress)
    if addr is None or len(addr) != 21:
        return None
    return addr[0]

if __name__ == '__main__':
    # Test case
    assert get_bcaddress_version('15VjRaDX9zpbA8LVnbrCAFzrVzN7ixHNsC') == 0
    _ohai = b'o hai'
    _tmp = b58encode(_ohai)
    assert _tmp == 'DYB3oMS'
    assert b58decode(_tmp, 5) == _ohai
    print("Tests passed")
