#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include "scalar.hpp"
#include "XKCP/Keccak-more-compact.hpp"
#include "config.hpp"
#include "utils.hpp"

/* Global scalar variables */

mpz_class ScalarMask4   ("F", 16);
mpz_class ScalarMask8   ("FF", 16);
mpz_class ScalarMask16  ("FFFF", 16);
mpz_class ScalarMask20  ("FFFFF", 16);
mpz_class ScalarMask32  ("FFFFFFFF", 16);
mpz_class ScalarMask64  ("FFFFFFFFFFFFFFFF", 16);
mpz_class ScalarMask256 ("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 16);
mpz_class ScalarTwoTo8  ("100", 16);
mpz_class ScalarTwoTo16 ("10000", 16);
mpz_class ScalarTwoTo18 ("40000", 16);
mpz_class ScalarTwoTo32 ("100000000", 16);
mpz_class ScalarTwoTo64 ("10000000000000000", 16);
mpz_class ScalarTwoTo128("100000000000000000000000000000000", 16);
mpz_class ScalarTwoTo192("1000000000000000000000000000000000000000000000000", 16);
mpz_class ScalarTwoTo256("10000000000000000000000000000000000000000000000000000000000000000", 16);
mpz_class ScalarTwoTo255("8000000000000000000000000000000000000000000000000000000000000000", 16);
mpz_class ScalarTwoTo258("40000000000000000000000000000000000000000000000000000000000000000", 16);
mpz_class ScalarZero    ("0", 16);
mpz_class ScalarOne     ("1", 16);
mpz_class ScalarGoldilocksPrime = (uint64_t)GOLDILOCKS_PRIME;

/* Scalar to/from a Sparse Merkle Tree key, interleaving bits */

void scalar2key (Goldilocks &fr, mpz_class &s, Goldilocks::Element (&key)[4])
{
    mpz_class auxk[4] = {0, 0, 0, 0};
    mpz_class r = s;
    mpz_class one = 1;
    uint64_t i = 0;

    while (r != 0)
    {
        if ((r&1) != 0)
        {
            auxk[i%4] = auxk[i%4] + (one << i/4);
        }
        r = r >> 1;
        i++;
    }

    for (uint64_t j=0; j<4; j++) key[j] = fr.fromU64(auxk[j].get_ui());
}

/* Hexa string to/from field element (array) conversion */

void string2fe (Goldilocks &fr, const string &s, Goldilocks::Element &fe)
{
    fr.fromString(fe, Remove0xIfPresent(s), 16);
}

string fea2string (Goldilocks &fr, const Goldilocks::Element(&fea)[4])
{
    mpz_class auxScalar;
    fea2scalar(fr, auxScalar, fea);
    return auxScalar.get_str(16);
}

string fea2string (Goldilocks &fr, const Goldilocks::Element &fea0, const Goldilocks::Element &fea1, const Goldilocks::Element &fea2, const Goldilocks::Element &fea3)
{
    const Goldilocks::Element fea[4] = {fea0, fea1, fea2, fea3};
    return fea2string(fr, fea);
}

/* Normalized strings */

string Remove0xIfPresent(const string &s)
{
    if ( (s.size() >= 2) && (s.at(1) == 'x') && (s.at(0) == '0') ) return s.substr(2);
    return s;
}

string Add0xIfMissing(const string &s)
{
    if ( (s.size() >= 2) && (s.at(1) == 'x') && (s.at(0) == '0') ) return s;
    return "0x" + s;
}


// A set of strings with zeros is available in memory for performance reasons
string sZeros[64] = {
    "",
    "0",
    "00",
    "000",
    "0000",
    "00000",
    "000000",
    "0000000",
    "00000000",
    "000000000",
    "0000000000",
    "00000000000",
    "000000000000",
    "0000000000000",
    "00000000000000",
    "000000000000000",
    "0000000000000000",
    "00000000000000000",
    "000000000000000000",
    "0000000000000000000",
    "00000000000000000000",
    "000000000000000000000",
    "0000000000000000000000",
    "00000000000000000000000",
    "000000000000000000000000",
    "0000000000000000000000000",
    "00000000000000000000000000",
    "000000000000000000000000000",
    "0000000000000000000000000000",
    "00000000000000000000000000000",
    "000000000000000000000000000000",
    "0000000000000000000000000000000",
    "00000000000000000000000000000000",
    "000000000000000000000000000000000",
    "0000000000000000000000000000000000",
    "00000000000000000000000000000000000",
    "000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "00000000000000000000000000000000000000",
    "000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000",
    "00000000000000000000000000000000000000000",
    "000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000",
    "00000000000000000000000000000000000000000000",
    "000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000",
    "00000000000000000000000000000000000000000000000",
    "000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000",
    "00000000000000000000000000000000000000000000000000",
    "000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000",
    "00000000000000000000000000000000000000000000000000000",
    "000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000",
    "00000000000000000000000000000000000000000000000000000000",
    "000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000",
    "00000000000000000000000000000000000000000000000000000000000",
    "000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000",
    "00000000000000000000000000000000000000000000000000000000000000",
    "000000000000000000000000000000000000000000000000000000000000000"
};

string PrependZeros (const string &s, uint64_t n)
{
    // Check that n is not too big
    if (n > 64)
    {
        cerr << "Error: PrependZeros() called with an that is too big n=" << n << endl;
        exitProcess();
    }
    // Check that string size is not too big
    uint64_t stringSize = s.size();
    if ( (stringSize > n) || (stringSize > 64) )
    {
        cerr << "Error: PrependZeros() called with a string with too large s.size=" << stringSize << " n=" << n << endl;
        exitProcess();
    }

    // Prepend zeros if needed
    if (stringSize < n) return sZeros[n-stringSize] + s;

    return s;
}

void PrependZeros (string &s, uint64_t n)
{
    // Check that n is not too big
    if (n > 64)
    {
        cerr << "Error: PrependZeros() called with an n that is too big n=" << n << endl;
        exitProcess();
    }
    // Check that string size is not too big
    uint64_t stringSize = s.size();
    if ( (stringSize > n) || (stringSize > 64) )
    {
        cerr << "Error: PrependZeros() called with a string with too large s.size=" << stringSize << " n=" << n << endl;
        exitProcess();
    }

    // Prepend zeros if needed
    if (stringSize < n) s = sZeros[n-stringSize] + s;
}

string NormalizeToNFormat (const string &s, uint64_t n)
{
    return PrependZeros(Remove0xIfPresent(s), n);
}

string NormalizeTo0xNFormat (const string &s, uint64_t n)
{
    return "0x" + NormalizeToNFormat(s, n);
}

string stringToLower (const string &s)
{
    string result = s;
    transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

/* Keccak */

void keccak256(const uint8_t *pInputData, uint64_t inputDataSize, uint8_t *pOutputData, uint64_t outputDataSize)
{
    Keccak(1088, 512, pInputData, inputDataSize, 0x1, pOutputData, outputDataSize);
}

void keccak256 (const uint8_t *pInputData, uint64_t inputDataSize, uint8_t (&hash)[32])
{
    Keccak(1088, 512, pInputData, inputDataSize, 0x1, hash, 32);
}

void keccak256 (const uint8_t *pInputData, uint64_t inputDataSize, mpz_class &hash)
{
    uint8_t hashBytes[32] = {0};
    keccak256(pInputData, inputDataSize, hashBytes);
    ba2scalar(hash, hashBytes);
}

string keccak256 (const uint8_t *pInputData, uint64_t inputDataSize)
{
    uint8_t hash[32] = {0};
    keccak256(pInputData, inputDataSize, hash);

    string s;
    ba2string(s, hash, 32);
    return "0x" + s;
}

void keccak256 (const vector<uint8_t> &input, mpz_class &hash)
{
    string baString;
    uint64_t inputSize = input.size();
    for (uint64_t i=0; i<inputSize; i++)
    {
        baString.push_back(input[i]);
    }
    keccak256((uint8_t *)baString.c_str(), baString.size(), hash);
}

/* Byte to/from char conversion */

uint8_t char2byte (char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    cerr << "Error: char2byte() called with an invalid, non-hex char: " << c << endl;
    exitProcess();
    return 0;
}

char byte2char (uint8_t b)
{
    if (b < 10) return '0' + b;
    if (b < 16) return 'A' + b - 10;
    cerr << "Error: byte2char() called with an invalid byte: " << b << endl;
    exitProcess();
    return 0;
}

string byte2string(uint8_t b)
{
    string result;
    result.push_back(byte2char(b >> 4));
    result.push_back(byte2char(b & 0x0F));
    return result;
}

/* Strint to/from byte array conversion
   s must be even sized, and must not include the leading "0x"
   pData buffer must be big enough to store converted data */

uint64_t string2ba (const string &os, uint8_t *pData, uint64_t &dataSize)
{
    string s = Remove0xIfPresent(os);

    if (s.size()%2 != 0)
    {
        s = "0" + s;
    }

    uint64_t dsize = s.size()/2;
    if (dsize > dataSize)
    {
        cerr << "Error: string2ba() called with a too short buffer: " << dsize << ">" << dataSize << endl;
        exitProcess();
    }

    const char *p = s.c_str();
    for (uint64_t i=0; i<dsize; i++)
    {
        pData[i] = char2byte(p[2*i])*16 + char2byte(p[2*i + 1]);
    }
    return dsize;
}

void string2ba (const string &textString, string &baString)
{
    baString.clear();

    string s = Remove0xIfPresent(textString);

    if (s.size()%2 != 0)
    {
        s = "0" + s;
    }

    uint64_t dsize = s.size()/2;

    const char *p = s.c_str();
    for (uint64_t i=0; i<dsize; i++)
    {
        uint8_t aux = char2byte(p[2*i])*16 + char2byte(p[2*i + 1]);
        baString.push_back(aux);
    }
}

string string2ba (const string &textString)
{
    string result;
    string2ba(textString, result);
    return result;
}

void ba2string (string &s, const uint8_t *pData, uint64_t dataSize)
{
    s = "";
    for (uint64_t i=0; i<dataSize; i++)
    {
        s.append(1, byte2char(pData[i] >> 4));
        s.append(1, byte2char(pData[i] & 0x0F));
    }
}

string ba2string (const uint8_t *pData, uint64_t dataSize)
{
    string result;
    ba2string(result, pData, dataSize);
    return result;
}

void ba2string (const string &baString, string &textString)
{
    ba2string(textString, (const uint8_t *)baString.c_str(), baString.size());
}

string ba2string (const string &baString)
{
    string result;
    ba2string(result, (const uint8_t *)baString.c_str(), baString.size());
    return result;
}

/* Byte array of exactly 2 bytes conversion */

void ba2u16 (const uint8_t *pData, uint16_t &n)
{
    n = pData[0]*256 + pData[1];
}

void ba2u32 (const uint8_t *pData, uint32_t &n)
{
    n = uint32_t(pData[0])*256*256*256 + uint32_t(pData[1])*256*256 + uint32_t(pData[2])*256 + uint32_t(pData[3]);
}

void ba2scalar (const uint8_t *pData, uint64_t dataSize, mpz_class &s)
{
    s = 0;
    for (uint64_t i=0; i<dataSize; i++)
    {
        s *= ScalarTwoTo8;
        s += pData[i];
    }
}

/* Scalar to byte array conversion (up to dataSize bytes) */

void scalar2ba (uint8_t *pData, uint64_t &dataSize, mpz_class s)
{
    uint64_t i=0;
    for (; i<dataSize; i++)
    {
        // Shift left 1B the byte array content
        for (uint64_t j=i; j>0; j--) pData[j] = pData[j-1];

        // Add the next byte to the byte array
        mpz_class auxScalar = s & ScalarMask8;
        pData[0] = auxScalar.get_ui();

        // Shift right 1B the scalar content
        s = s >> 8;

        // When we run out of significant bytes, break
        if (s == ScalarZero) break;
    }
    if (s != ScalarZero)
    {
        cerr << "Error: scalar2ba() run out of buffer of " << dataSize << " bytes" << endl;
        exitProcess();
    }
    dataSize = i+1;
}

void scalar2ba16(uint64_t *pData, uint64_t &dataSize, mpz_class s)
{
    memset(pData, 0, dataSize*sizeof(uint64_t));
    uint64_t i=0;
    for (; i<dataSize; i++)
    {
        // Add the next byte to the byte array
        mpz_class auxScalar = s & ( (i<(dataSize-1)) ? ScalarMask16 : ScalarMask20 );
        pData[i] = auxScalar.get_ui();

        // Shift right 2 bytes the scalar content
        s = s >> 16;

        // When we run out of significant bytes, break
        if (s == ScalarZero) break;
    }
    if (s > ScalarMask4)
    {
        cerr << "Error: scalar2ba16() run out of buffer of " << dataSize << " bytes" << endl;
        exitProcess();
    }
    dataSize = i+1;
}

void scalar2bytes(mpz_class &s, uint8_t (&bytes)[32])
{
    for (uint64_t i=0; i<32; i++)
    {
        mpz_class aux = s & ScalarMask8;
        bytes[i] = aux.get_ui();
        s = s >> 8;
    }
    if (s != ScalarZero)
    {
        cerr << "Error: scalar2bytes() run out of space of 32 bytes" << endl;
        exitProcess();
    }
}

/* Scalar to byte array string conversion */

string scalar2ba(const mpz_class &s)
{
    uint64_t size = mpz_sizeinbase(s.get_mpz_t(), 256);
    if (size > 32)
    {
        cerr << "Error: scalar2ba() failed, size=" << size << " is > 32" << endl;
        exitProcess();
    }

    uint8_t buffer[32];
    mpz_export(buffer, NULL, 1, 1, 1, 0, s.get_mpz_t());

    string result;
    for (uint64_t i = 0; i < size; i++)
    {
        result.push_back(buffer[i]);
    }
    
    return result;
}

/* Converts a scalar to a vector of bits of the scalar, with value 1 or 0; bits[0] is least significant bit */

void scalar2bits(mpz_class s, vector<uint8_t> &bits)
{
    while (s > ScalarZero)
    {
        if ((s & 1) == ScalarOne)
        {
            bits.push_back(1);
        }
        else
        {
            bits.push_back(0);
        }
        s = s >> 1;
    }
}

/* Byte to/from bits array conversion, with value 1 or 0; bits[0] is the least significant bit */

void byte2bits(uint8_t byte, uint8_t *pBits)
{
    for (uint64_t i=0; i<8; i++)
    {
        if ((byte&1) == 1)
        {
            pBits[i] = 1;
        }
        else
        {
            pBits[i] = 0;
        }
        byte = byte >> 1;
    }
}

void bits2byte(const uint8_t *pBits, uint8_t &byte)
{
    byte = 0;
    for (uint64_t i=0; i<8; i++)
    {
        byte = byte << 1;
        if ((pBits[7-i]&0x01) == 1)
        {
            byte |= 1;
        }
    }
}

/* 8 fe to/from 4 fe conversion */

void sr8to4 ( Goldilocks &fr,
              Goldilocks::Element a0,
              Goldilocks::Element a1,
              Goldilocks::Element a2,
              Goldilocks::Element a3,
              Goldilocks::Element a4,
              Goldilocks::Element a5,
              Goldilocks::Element a6,
              Goldilocks::Element a7,
              Goldilocks::Element &r0,
              Goldilocks::Element &r1,
              Goldilocks::Element &r2,
              Goldilocks::Element &r3 )
{
    r0 = fr.fromU64(fr.toU64(a0) + (fr.toU64(a1)<<32));
    r1 = fr.fromU64(fr.toU64(a2) + (fr.toU64(a3)<<32));
    r2 = fr.fromU64(fr.toU64(a4) + (fr.toU64(a5)<<32));
    r3 = fr.fromU64(fr.toU64(a6) + (fr.toU64(a7)<<32));
}

void sr4to8 ( Goldilocks &fr,
              Goldilocks::Element a0,
              Goldilocks::Element a1,
              Goldilocks::Element a2,
              Goldilocks::Element a3,
              Goldilocks::Element &r0,
              Goldilocks::Element &r1,
              Goldilocks::Element &r2,
              Goldilocks::Element &r3,
              Goldilocks::Element &r4,
              Goldilocks::Element &r5,
              Goldilocks::Element &r6,
              Goldilocks::Element &r7 )
{
    uint64_t aux;

    aux = fr.toU64(a0);
    r0 = fr.fromU64( aux & 0xFFFFFFFF );
    r1 = fr.fromU64( aux >> 32 );

    aux = fr.toU64(a1);
    r2 = fr.fromU64( aux & 0xFFFFFFFF );
    r3 = fr.fromU64( aux >> 32 );

    aux = fr.toU64(a2);
    r4 = fr.fromU64( aux & 0xFFFFFFFF );
    r5 = fr.fromU64( aux >> 32 );

    aux = fr.toU64(a3);
    r6 = fr.fromU64( aux & 0xFFFFFFFF );
    r7 = fr.fromU64( aux >> 32 );
}

/* Scalar to/from fec conversion */

void fec2scalar (RawFec &fec, const RawFec::Element &fe, mpz_class &s)
{
    s.set_str(fec.toString(fe,16),16);
}
void scalar2fec (RawFec &fec, RawFec::Element &fe, const mpz_class &s)
{
    fec.fromMpz(fe, s.get_mpz_t());
}

void u642bytes (uint64_t input, uint8_t * pOutput, bool bBigEndian)
{
    for (uint64_t i=0; i<8; i++)
    {
        pOutput[bBigEndian ? (7-i) : i] = input & 0x00000000000000FF;
        if (i != 7) input = input >> 8;
    }
}

void bytes2u32 (const uint8_t * pInput, uint32_t &output, bool bBigEndian)
{
    output = 0;
    for (uint64_t i=0; i<4; i++)
    {
        if (i != 0) output = output << 8;
        output |= pInput[bBigEndian ? i : (4-i)];
    }
}

void bytes2u64 (const uint8_t * pInput, uint64_t &output, bool bBigEndian)
{
    output = 0;
    for (uint64_t i=0; i<8; i++)
    {
        if (i != 0) output = output << 8;
        output |= pInput[bBigEndian ? i : (7-i)];
    }
}

uint64_t swapBytes64 (uint64_t input)
{
    return ((((input) & 0xff00000000000000ull) >> 56) |
            (((input) & 0x00ff000000000000ull) >> 40) |
            (((input) & 0x0000ff0000000000ull) >> 24) |
            (((input) & 0x000000ff00000000ull) >> 8 ) |
            (((input) & 0x00000000ff000000ull) << 8 ) |
            (((input) & 0x0000000000ff0000ull) << 24) |
            (((input) & 0x000000000000ff00ull) << 40) |
            (((input) & 0x00000000000000ffull) << 56));
}