#include "./zim_decompress.h"

#include <zstd.h>

namespace
{

const size_t CHUNK = 128 * 1024;

// A ceiling on one decompressed cluster. zim-tools writes ~2MB; anything approaching this
// is a corrupt or hostile archive, and unbounded growth on a 128MB device is fatal.
const size_t MAX_CLUSTER_BYTES = 64 * 1024 * 1024;

bool decompress_zstd(const char *in, size_t in_len, std::string &out)
{
    ZSTD_DStream *stream = ZSTD_createDStream();
    if (stream == nullptr)
    {
        return false;
    }

    if (ZSTD_isError(ZSTD_initDStream(stream)))
    {
        ZSTD_freeDStream(stream);
        return false;
    }

    ZSTD_inBuffer input{in, in_len, 0};
    bool ok = true;
    size_t rc = 1;   // non-zero: a frame is in progress

    // Also while rc > 0: that means output is still buffered inside the stream, which
    // happens whenever the chunk fills on the same call that drains the input. Stopping
    // at input.pos == input.size alone silently truncated the cluster and still reported
    // success, and the caller then blamed the blob offset table.
    while (input.pos < input.size || rc > 0)
    {
        const size_t at = out.size();
        if (at + CHUNK > MAX_CLUSTER_BYTES)
        {
            ok = false;
            break;
        }
        out.resize(at + CHUNK);

        ZSTD_outBuffer output{&out[0] + at, CHUNK, 0};
        const size_t prev_in = input.pos;
        rc = ZSTD_decompressStream(stream, &output, &input);
        out.resize(at + output.pos);

        if (ZSTD_isError(rc))
        {
            ok = false;
            break;
        }

        // A finished frame with input to spare means a second frame follows; keep going.
        if (rc == 0 && input.pos < input.size)
        {
            if (ZSTD_isError(ZSTD_initDStream(stream)))
            {
                ok = false;
                break;
            }
            rc = 1;
            continue;
        }

        // Neither side moved: the frame is truncated. Report it rather than handing back
        // a short cluster that looks valid.
        if (output.pos == 0 && input.pos == prev_in)
        {
            ok = rc == 0;
            break;
        }
    }

    ZSTD_freeDStream(stream);
    return ok;
}

}

namespace zim
{

const char *compression_name(Compression algo)
{
    switch (algo)
    {
        case Compression::Default: return "none";
        case Compression::None: return "none";
        case Compression::Zlib: return "zlib";
        case Compression::Bzip2: return "bzip2";
        case Compression::Lzma: return "lzma";
        case Compression::Zstd: return "zstd";
    }
    return "unknown";
}

bool compression_supported(Compression algo)
{
    return algo == Compression::Default || algo == Compression::None || algo == Compression::Zstd;
}

bool zim_decompress(Compression algo, const char *in, size_t in_len, std::string &out)
{
    out.clear();

    switch (algo)
    {
        case Compression::Default:
        case Compression::None:
            out.assign(in, in_len);
            return true;

        case Compression::Zstd:
            return decompress_zstd(in, in_len, out);

        case Compression::Zlib:
        case Compression::Bzip2:
        case Compression::Lzma:
            return false;
    }

    return false;
}

}
