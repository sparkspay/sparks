// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_HASH_H
#define BITCOIN_HASH_H

#include <attributes.h>
#include <crypto/common.h>
#include <crypto/ripemd160.h>
#include <crypto/sha256.h>
#include <prevector.h>
#include <serialize.h>
#include <uint256.h>
#include <version.h>

#include <crypto/sph_blake.h>
#include <crypto/sph_bmw.h>
#include <crypto/sph_groestl.h>
#include <crypto/sph_jh.h>
#include <crypto/sph_keccak.h>
#include <crypto/sph_skein.h>
#include <crypto/sph_luffa.h>
#include <crypto/sph_cubehash.h>
#include <crypto/sph_shavite.h>
#include <crypto/sph_simd.h>
#include <crypto/sph_echo.h>

#include <vector>

typedef uint256 ChainCode;

/* ----------- Bitcoin Hash ------------------------------------------------- */
/** A hasher class for Bitcoin's 256-bit hash (double SHA-256). */
class CHash256 {
private:
    CSHA256 sha;
public:
    static const size_t OUTPUT_SIZE = CSHA256::OUTPUT_SIZE;

    void Finalize(Span<unsigned char> output) {
        assert(output.size() == OUTPUT_SIZE);
        unsigned char buf[CSHA256::OUTPUT_SIZE];
        sha.Finalize(buf);
        sha.Reset().Write(buf, CSHA256::OUTPUT_SIZE).Finalize(output.data());
    }

    CHash256& Write(Span<const unsigned char> input) {
        sha.Write(input.data(), input.size());
        return *this;
    }

    CHash256& Reset() {
        sha.Reset();
        return *this;
    }
};

/** A hasher class for Bitcoin's 160-bit hash (SHA-256 + RIPEMD-160). */
class CHash160 {
private:
    CSHA256 sha;
public:
    static const size_t OUTPUT_SIZE = CRIPEMD160::OUTPUT_SIZE;

    void Finalize(Span<unsigned char> output) {
        assert(output.size() == OUTPUT_SIZE);
        unsigned char buf[CSHA256::OUTPUT_SIZE];
        sha.Finalize(buf);
        CRIPEMD160().Write(buf, CSHA256::OUTPUT_SIZE).Finalize(output.data());
    }

    CHash160& Write(Span<const unsigned char> input) {
        sha.Write(input.data(), input.size());
        return *this;
    }

    CHash160& Reset() {
        sha.Reset();
        return *this;
    }
};

/** Compute the 256-bit hash of an object. */
template<typename T>
inline uint256 Hash(const T& in1)
{
    uint256 result;
    CHash256().Write(MakeUCharSpan(in1)).Finalize(result);
    return result;
}

/** Compute the 256-bit hash of the concatenation of two objects. */
template<typename T1, typename T2>
inline uint256 Hash(const T1& in1, const T2& in2) {
    uint256 result;
    CHash256().Write(MakeUCharSpan(in1)).Write(MakeUCharSpan(in2)).Finalize(result);
    return result;
}

/** Compute the 160-bit hash an object. */
template<typename T1>
inline uint160 Hash160(const T1& in1)
{
    uint160 result;
    CHash160().Write(MakeUCharSpan(in1)).Finalize(result);
    return result;
}

/** A writer stream (for serialization) that computes a 256-bit hash. */
class CHashWriter
{
private:
    CSHA256 ctx;

    const int nType;
    const int nVersion;
public:

    CHashWriter(int nTypeIn, int nVersionIn) : nType(nTypeIn), nVersion(nVersionIn) {}

    int GetType() const { return nType; }
    int GetVersion() const { return nVersion; }

    void write(Span<const std::byte> src)
    {
        ctx.Write(UCharCast(src.data()), src.size());
    }

    /** Compute the double-SHA256 hash of all data written to this object.
     *
     * Invalidates this object.
     */
    uint256 GetHash() {
        uint256 result;
        ctx.Finalize(result.begin());
        ctx.Reset().Write(result.begin(), CSHA256::OUTPUT_SIZE).Finalize(result.begin());
        return result;
    }

    /** Compute the SHA256 hash of all data written to this object.
     *
     * Invalidates this object.
     */
    uint256 GetSHA256() {
        uint256 result;
        ctx.Finalize(result.begin());
        return result;
    }

    /**
     * Returns the first 64 bits from the resulting hash.
     */
    inline uint64_t GetCheapHash() {
        uint256 result = GetHash();
        return ReadLE64(result.begin());
    }

    template<typename T>
    CHashWriter& operator<<(const T& obj) {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }
};

/** Reads data from an underlying stream, while hashing the read data. */
template<typename Source>
class CHashVerifier : public CHashWriter
{
private:
    Source* source;

public:
    explicit CHashVerifier(Source* source_) : CHashWriter(source_->GetType(), source_->GetVersion()), source(source_) {}

    void read(Span<std::byte> dst)
    {
        source->read(dst);
        this->write(dst);
    }

    void ignore(size_t nSize)
    {
        std::byte data[1024];
        while (nSize > 0) {
            size_t now = std::min<size_t>(nSize, 1024);
            read({data, now});
            nSize -= now;
        }
    }

    template<typename T>
    CHashVerifier<Source>& operator>>(T&& obj)
    {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }
};

/** Writes data to an underlying source stream, while hashing the written data. */
template <typename Source>
class HashedSourceWriter : public CHashWriter
{
private:
    Source& m_source;

public:
    explicit HashedSourceWriter(Source& source LIFETIMEBOUND) : CHashWriter{source.GetType(), source.GetVersion()}, m_source{source} {}

    void write(Span<const std::byte> src)
    {
        m_source.write(src);
        CHashWriter::write(src);
    }

    template <typename T>
    HashedSourceWriter& operator<<(const T& obj)
    {
        ::Serialize(*this, obj);
        return *this;
    }
};

/** Compute the 256-bit hash of an object's serialization. */
template<typename T>
uint256 SerializeHash(const T& obj, int nType=SER_GETHASH, int nVersion=PROTOCOL_VERSION)
{
    CHashWriter ss(nType, nVersion);
    ss << obj;
    return ss.GetHash();
}

/** Single-SHA256 a 32-byte input (represented as uint256). */
[[nodiscard]] uint256 SHA256Uint256(const uint256& input);

unsigned int MurmurHash3(unsigned int nHashSeed, Span<const unsigned char> vDataToHash);

void BIP32Hash(const ChainCode &chainCode, unsigned int nChild, unsigned char header, const unsigned char data[32], unsigned char output[64]);

/* ----------- Sparks Hash ------------------------------------------------ */
template<typename T1>
inline uint256 HashX11(const T1 pbegin, const T1 pend)

{
    sph_blake512_context     ctx_blake;
    sph_bmw512_context       ctx_bmw;
    sph_groestl512_context   ctx_groestl;
    sph_jh512_context        ctx_jh;
    sph_keccak512_context    ctx_keccak;
    sph_skein512_context     ctx_skein;
    sph_luffa512_context     ctx_luffa;
    sph_cubehash512_context  ctx_cubehash;
    sph_shavite512_context   ctx_shavite;
    sph_simd512_context      ctx_simd;
    sph_echo512_context      ctx_echo;
    static unsigned char pblank[1];

    uint512 hash[11];

    sph_blake512_init(&ctx_blake);
    sph_blake512 (&ctx_blake, (pbegin == pend ? pblank : static_cast<const void*>(&pbegin[0])), (pend - pbegin) * sizeof(pbegin[0]));
    sph_blake512_close(&ctx_blake, static_cast<void*>(&hash[0]));

    sph_bmw512_init(&ctx_bmw);
    sph_bmw512 (&ctx_bmw, static_cast<const void*>(&hash[0]), 64);
    sph_bmw512_close(&ctx_bmw, static_cast<void*>(&hash[1]));

    sph_groestl512_init(&ctx_groestl);
    sph_groestl512 (&ctx_groestl, static_cast<const void*>(&hash[1]), 64);
    sph_groestl512_close(&ctx_groestl, static_cast<void*>(&hash[2]));

    sph_skein512_init(&ctx_skein);
    sph_skein512 (&ctx_skein, static_cast<const void*>(&hash[2]), 64);
    sph_skein512_close(&ctx_skein, static_cast<void*>(&hash[3]));

    sph_jh512_init(&ctx_jh);
    sph_jh512 (&ctx_jh, static_cast<const void*>(&hash[3]), 64);
    sph_jh512_close(&ctx_jh, static_cast<void*>(&hash[4]));

    sph_keccak512_init(&ctx_keccak);
    sph_keccak512 (&ctx_keccak, static_cast<const void*>(&hash[4]), 64);
    sph_keccak512_close(&ctx_keccak, static_cast<void*>(&hash[5]));

    sph_luffa512_init(&ctx_luffa);
    sph_luffa512 (&ctx_luffa, static_cast<void*>(&hash[5]), 64);
    sph_luffa512_close(&ctx_luffa, static_cast<void*>(&hash[6]));

    sph_cubehash512_init(&ctx_cubehash);
    sph_cubehash512 (&ctx_cubehash, static_cast<const void*>(&hash[6]), 64);
    sph_cubehash512_close(&ctx_cubehash, static_cast<void*>(&hash[7]));

    sph_shavite512_init(&ctx_shavite);
    sph_shavite512(&ctx_shavite, static_cast<const void*>(&hash[7]), 64);
    sph_shavite512_close(&ctx_shavite, static_cast<void*>(&hash[8]));

    sph_simd512_init(&ctx_simd);
    sph_simd512 (&ctx_simd, static_cast<const void*>(&hash[8]), 64);
    sph_simd512_close(&ctx_simd, static_cast<void*>(&hash[9]));

    sph_echo512_init(&ctx_echo);
    sph_echo512 (&ctx_echo, static_cast<const void*>(&hash[9]), 64);
    sph_echo512_close(&ctx_echo, static_cast<void*>(&hash[10]));

    return hash[10].trim256();
}

#endif // BITCOIN_HASH_H
