#pragma once

// #142 (WebAuthn real-signature verification): a thin, exception-free
// wrapper over libcbor's cbor_load() for parsing the two CBOR payloads a
// WebAuthn server must read -- attestationObject (registration) and the
// COSE key embedded in authenticatorData. cbor_load() by itself will
// happily assemble arbitrarily deep / arbitrarily large trees from a
// client-controlled buffer, so every entry point here enforces the same
// hard limits BEFORE the tree is materialized:
//
//   - total input          <= 131072 bytes (128 KiB)
//   - nesting depth        <= 8 containers
//   - total decoded items  <= 1024
//   - byte/text strings    <= 65536 bytes
//   - indefinite-length items (0x1f additional info) are rejected outright
//
// Errors are reported the way the rest of the WebAuthn crypto core does it:
// std::nullopt plus an optional human-readable reason in *errorOut. Nothing
// in this header throws.

#include <cbor.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fulla::identity::webauthn
{

// ---------------------------------------------------------------------------
// Hard limits shared by load()/measure() (see header comment). Exposed as
// constexpr so callers/tests can reference the exact thresholds.
// ---------------------------------------------------------------------------
namespace limits
{
constexpr size_t kMaxInputBytes = 131072;  // 128 KiB total CBOR input
constexpr size_t kMaxDepth = 8;            // nesting levels of containers
constexpr size_t kMaxItems = 1024;         // decoded items, containers included
constexpr size_t kMaxStringBytes = 65536;  // single byte/text string payload
}  // namespace limits

namespace detail
{

// Human-readable name for a cbor_load error code (libcbor has no
// cbor_error_to_string()).
inline const char *cborErrorName(cbor_error_code code)
{
    switch (code)
    {
        case CBOR_ERR_NONE:
            return "no error";
        case CBOR_ERR_NOTENOUGHDATA:
            return "not enough data";
        case CBOR_ERR_NODATA:
            return "no data";
        case CBOR_ERR_MALFORMATED:
            return "malformed item";
        case CBOR_ERR_MEMERROR:
            return "allocation failure";
        case CBOR_ERR_SYNTAXERROR:
            return "syntax error";
        default:
            return "unknown error";
    }
}

// Structural pre-scan over [data, data + size). Decodes just enough CBOR
// structure (initial bytes, argument widths, definite lengths) to enforce
// the limits above without materializing any tree -- this is what keeps a
// hostile 128 KiB buffer from OOM-ing or deep-recursing inside cbor_load().
// On success returns the exact encoded length of the FIRST item (which may
// be smaller than `size` -- callers decide what to do with trailing bytes;
// load() rejects them, measure() is precisely for measuring that prefix).
inline std::optional<size_t> validateStructure(const unsigned char *data, size_t size, std::string *errorOut)
{
    const auto fail = [errorOut](const char *why) {
        if (errorOut != nullptr)
            *errorOut = why;
        return std::nullopt;
    };

    if (size == 0)
        return fail("empty CBOR input");

    // `pending` holds the still-expected child count of every open
    // container (maps count key+value as two slots; tags wrap exactly one
    // item but do not count as a nesting level of their own). Its size IS
    // the current nesting depth.
    std::vector<size_t> pending;
    size_t pos = 0;
    size_t itemCount = 0;

    while (true)
    {
        if (pos >= size)
            return fail("truncated CBOR: expected another item");

        // This item satisfies one slot of the innermost open container
        // (no-op for the root item). Frames whose count just dropped to
        // zero are retired only AFTER the item is parsed -- retiring
        // earlier would (a) underflow a just-pushed empty-container frame
        // (0 - 1 wraps on size_t) and (b) erase the depth context of
        // single-element chains like 81 81 81 ... before their children
        // are parsed.
        if (!pending.empty())
        {
            if (pending.back() == 0)
                return fail("internal error: retired frame pending");
            --pending.back();
        }

        const unsigned char initial = data[pos++];
        const unsigned char major = static_cast<unsigned char>(initial >> 5);
        const unsigned char info = static_cast<unsigned char>(initial & 0x1F);

        if (info == 31)
            return fail("indefinite-length items are not accepted");
        if (info >= 28)  // 28..30 are reserved additional-info values
            return fail("reserved additional-info value");

        // Argument width for info 24..27 is 1/2/4/8 bytes; majors 0..6 read
        // a definite length/value from them. Major 7 uses 25/26/27 for
        // half/single/double floats instead (handled below).
        size_t argBytes = 0;
        if (info >= 24 && info <= 27)
            argBytes = (info == 24) ? 1 : (info == 25) ? 2 : (info == 26) ? 4 : 8;

        uint64_t argument = info;
        if (argBytes > 0 && major != 7)
        {
            if (argBytes > size - pos)
                return fail("truncated CBOR: argument bytes missing");
            argument = 0;  // extended argument REPLACES the info value
            for (size_t i = 0; i < argBytes; ++i)
                argument = (argument << 8) | static_cast<uint64_t>(data[pos + i]);
            pos += argBytes;
        }

        if (++itemCount > limits::kMaxItems)
            return fail("too many CBOR items");

        switch (major)
        {
            case 0:  // unsigned integer: value carried in the argument
            case 1:  // negative integer: -1 - argument
                break;
            case 2:  // byte string
            case 3:  // text string
                if (argument > limits::kMaxStringBytes)
                    return fail("string payload exceeds 65536 bytes");
                if (argument > size - pos)
                    return fail("truncated CBOR: string payload missing");
                pos += static_cast<size_t>(argument);
                break;
            case 4:  // array
                if (pending.size() + 1 > limits::kMaxDepth)
                    return fail("CBOR nesting deeper than 8 levels");
                pending.push_back(static_cast<size_t>(argument));
                break;
            case 5:  // map: N pairs = 2N child slots; guard the doubling
                if (argument > std::numeric_limits<size_t>::max() / 2)
                    return fail("map entry count overflows");
                if (pending.size() + 1 > limits::kMaxDepth)
                    return fail("CBOR nesting deeper than 8 levels");
                pending.push_back(static_cast<size_t>(argument) * 2);
                break;
            case 6:  // tag: wraps exactly one tagged item, no depth of its own
                pending.push_back(1);
                break;
            default:  // 7: floats and simple values
            {
                size_t payload = 0;
                if (info == 24)
                    payload = 1;  // simple value, one extension byte
                else if (info == 25)
                    payload = 2;  // half-precision float
                else if (info == 26)
                    payload = 4;  // single-precision float
                else if (info == 27)
                    payload = 8;  // double-precision float
                if (payload > size - pos)
                    return fail("truncated CBOR: float/simple payload missing");
                pos += payload;
                break;
            }
        }

        // Close out containers whose child count has dropped to zero.
        while (!pending.empty() && pending.back() == 0)
            pending.pop_back();
        if (pending.empty())
            return pos;  // root item fully consumed
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// CborNode: a NON-OWNING view of one item inside a CborReader's tree. Nodes
// stay valid as long as the CborReader that produced them is alive (the
// tree root owns everything; libcbor parent items hold references to their
// children). All accessors are total: wrong-type queries return nullopt
// instead of asserting or throwing.
// ---------------------------------------------------------------------------
class CborNode
{
public:
    CborNode() = default;

    explicit CborNode(cbor_item_t *item) : item_(item)
    {
    }

    bool valid() const
    {
        return item_ != nullptr;
    }

    // Raw libcbor item for callers needing something not wrapped here.
    // Never cbor_incref/cbor_decref it -- the owning CborReader does that.
    cbor_item_t *raw() const
    {
        return item_;
    }

    bool isMap() const
    {
        return item_ != nullptr && cbor_isa_map(item_);
    }
    bool isArray() const
    {
        return item_ != nullptr && cbor_isa_array(item_);
    }
    bool isString() const  // CBOR text string (major 3)
    {
        return item_ != nullptr && cbor_isa_string(item_);
    }
    bool isBytes() const  // CBOR byte string (major 2)
    {
        return item_ != nullptr && cbor_isa_bytestring(item_);
    }
    bool isUint() const  // major 0
    {
        return item_ != nullptr && cbor_isa_uint(item_);
    }
    bool isNint() const  // major 1
    {
        return item_ != nullptr && cbor_isa_negint(item_);
    }
    bool isInt() const  // major 0 or 1
    {
        return isUint() || isNint();
    }
    bool isBool() const
    {
        return item_ != nullptr && cbor_is_bool(item_);
    }
    bool isNull() const
    {
        return item_ != nullptr && cbor_is_null(item_);
    }

    std::optional<size_t> mapSize() const
    {
        if (!isMap())
            return std::nullopt;
        return cbor_map_size(item_);
    }

    // Value stored under a text-string key equal to `key`. Non-string keys
    // and missing keys yield nullopt; on duplicate keys the first wins
    // (WebAuthn structures never duplicate keys).
    std::optional<CborNode> mapLookup(const std::string &key) const
    {
        if (!isMap())
            return std::nullopt;
        const size_t count = cbor_map_size(item_);
        const struct cbor_pair *pairs = cbor_map_handle(item_);
        for (size_t i = 0; i < count; ++i)
        {
            if (pairs[i].key == nullptr || !cbor_isa_string(pairs[i].key))
                continue;
            const size_t len = cbor_string_length(pairs[i].key);
            if (len == key.size() && std::memcmp(cbor_string_handle(pairs[i].key), key.data(), len) == 0)
                return CborNode(pairs[i].value);
        }
        return std::nullopt;
    }

    // i-th (key, value) pair; out-of-range or non-map yields nullopt.
    std::optional<std::pair<CborNode, CborNode>> mapAt(size_t index) const
    {
        if (!isMap() || index >= cbor_map_size(item_))
            return std::nullopt;
        const struct cbor_pair &pair = cbor_map_handle(item_)[index];
        if (pair.key == nullptr || pair.value == nullptr)
            return std::nullopt;
        return std::make_pair(CborNode(pair.key), CborNode(pair.value));
    }

    std::optional<size_t> arraySize() const
    {
        if (!isArray())
            return std::nullopt;
        return cbor_array_size(item_);
    }

    std::optional<CborNode> arrayAt(size_t index) const
    {
        if (!isArray() || index >= cbor_array_size(item_))
            return std::nullopt;
        return CborNode(cbor_array_handle(item_)[index]);
    }

    std::optional<std::string> asString() const  // text string -> copy
    {
        if (!isString())
            return std::nullopt;
        return std::string(reinterpret_cast<const char *>(cbor_string_handle(item_)), cbor_string_length(item_));
    }

    std::optional<std::string> asBytes() const  // byte string -> raw bytes
    {
        if (!isBytes())
            return std::nullopt;
        return std::string(reinterpret_cast<const char *>(cbor_bytestring_handle(item_)),
                           cbor_bytestring_length(item_));
    }

    // Unsigned or negative integer narrowed to int64_t; out-of-range
    // values (> INT64_MAX, < INT64_MIN) yield nullopt rather than wrap.
    std::optional<int64_t> asInt() const
    {
        if (isUint())
        {
            const uint64_t value = cbor_get_int(item_);
            if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                return std::nullopt;
            return static_cast<int64_t>(value);
        }
        if (isNint())
        {
            // CBOR negative integers encode -1 - magnitude.
            const uint64_t magnitude = cbor_get_int(item_);
            if (magnitude > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                return std::nullopt;
            return -1 - static_cast<int64_t>(magnitude);
        }
        return std::nullopt;
    }

private:
    cbor_item_t *item_ = nullptr;
};

// ---------------------------------------------------------------------------
// CborReader: owns one cbor_item_t tree loaded from a CBOR byte string.
// ---------------------------------------------------------------------------
class CborReader
{
public:
    // Parses `bytes` as exactly one CBOR item (trailing bytes are an
    // error -- every WebAuthn payload is a single item). Enforces all the
    // limits from the header comment before libcbor allocates anything.
    static std::optional<CborReader> load(const std::string &bytes, std::string *errorOut = nullptr)
    {
        const auto fail = [errorOut](std::string reason) {
            if (errorOut != nullptr)
                *errorOut = std::move(reason);
            return std::nullopt;
        };

        try
        {
            if (bytes.size() > limits::kMaxInputBytes)
                return fail("input exceeds 131072 bytes");

            const unsigned char *data = reinterpret_cast<const unsigned char *>(bytes.data());
            const std::optional<size_t> consumed = detail::validateStructure(data, bytes.size(), errorOut);
            if (!consumed.has_value())
                return std::nullopt;  // errorOut already filled by validateStructure
            if (*consumed != bytes.size())
                return fail("trailing bytes after the CBOR item");

            struct cbor_load_result result;
            cbor_item_t *item = cbor_load(data, bytes.size(), &result);
            if (item == nullptr)
            {
                return fail(std::string("cbor_load failed: ") + detail::cborErrorName(result.error.code) +
                            " at byte " + std::to_string(result.error.position));
            }
            if (result.read != bytes.size())
            {
                // Cannot happen when the pre-scan agreed, but a cheap
                // cross-check beats trusting two decoders to stay in sync.
                cbor_decref(&item);
                return fail("cbor_load did not consume the whole input");
            }
            return CborReader(TreePtr(item));
        }
        catch (const std::exception &e)
        {
            return fail(std::string("unexpected exception: ") + e.what());
        }
        catch (...)
        {
            return fail("unexpected non-standard exception");
        }
    }

    // Encoded length of the FIRST CBOR item in [data, data + size), under
    // the same limits as load(). This is how WebAuthnCrypto.cc finds the
    // exact end of the COSE key and extension blocks inside authData
    // (authData embeds raw CBOR without outer framing, so the length must
    // be recovered by decoding, not by reading a prefix header).
    static std::optional<size_t> measure(const char *data, size_t size, std::string *errorOut = nullptr)
    {
        if (size > limits::kMaxInputBytes)
        {
            if (errorOut != nullptr)
                *errorOut = "input exceeds 131072 bytes";
            return std::nullopt;
        }
        try
        {
            return detail::validateStructure(reinterpret_cast<const unsigned char *>(data), size, errorOut);
        }
        catch (...)
        {
            if (errorOut != nullptr)
                *errorOut = "unexpected exception while measuring CBOR";
            return std::nullopt;
        }
    }

    CborNode root() const
    {
        return CborNode(root_.get());
    }

private:
    // cbor_decref() takes cbor_item_t** (it NULLs its argument after
    // freeing); handing it the address of the by-value deleter parameter
    // releases the tree and the NULL lands harmlessly on the local copy.
    struct TreeDeleter
    {
        void operator()(cbor_item_t *item) const
        {
            if (item != nullptr)
                cbor_decref(&item);
        }
    };
    using TreePtr = std::unique_ptr<cbor_item_t, TreeDeleter>;

    explicit CborReader(TreePtr root) : root_(std::move(root))
    {
    }

    TreePtr root_;
};

}  // namespace fulla::identity::webauthn
