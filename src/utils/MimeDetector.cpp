//
// Created by spenc on 7/22/2025.
//

#include "MimeDetector.h"
#include <algorithm>
#include <cstring>
#include <cctype>

static const std::unordered_map<std::string, MimeType> extensionMap = {
    {"txt", MimeType::TextPlain}, {"html", MimeType::TextHtml}, {"htm", MimeType::TextHtml},
    {"css", MimeType::TextCss}, {"js", MimeType::TextJavascript}, {"md", MimeType::TextMarkdown},
    {"csv", MimeType::TextCsv}, {"xml", MimeType::TextXml},

    {"png", MimeType::ImagePng}, {"jpg", MimeType::ImageJpeg}, {"jpeg", MimeType::ImageJpeg},
    {"gif", MimeType::ImageGif}, {"webp", MimeType::ImageWebp}, {"bmp", MimeType::ImageBmp},
    {"tiff", MimeType::ImageTiff}, {"tif", MimeType::ImageTiff}, {"svg", MimeType::ImageSvg},

    {"pdf", MimeType::ApplicationPdf}, {"zip", MimeType::ApplicationZip},
    {"gz", MimeType::ApplicationGzip}, {"json", MimeType::ApplicationJson},
    {"wasm", MimeType::ApplicationWasm}, {"xml", MimeType::ApplicationXml},
    {"doc", MimeType::ApplicationMsword}, {"xls", MimeType::ApplicationVndExcel},
    {"ppt", MimeType::ApplicationVndPowerpoint}, {"rtf", MimeType::ApplicationRtf},
    {"xhtml", MimeType::ApplicationXhtml},

    {"mp3", MimeType::AudioMpeg}, {"ogg", MimeType::AudioOgg}, {"wav", MimeType::AudioWav},
    {"webm", MimeType::AudioWebm}, {"aac", MimeType::AudioAac}, {"flac", MimeType::AudioFlac},

    {"mp4", MimeType::VideoMp4}, {"webm", MimeType::VideoWebm}, {"ogv", MimeType::VideoOgg},
    {"mpeg", MimeType::VideoMpeg}, {"avi", MimeType::VideoAvi}, {"mov", MimeType::VideoMov}
};

static const std::unordered_map<MimeType, std::string> mimeToString = {
    {MimeType::TextPlain, "text/plain"}, {MimeType::TextHtml, "text/html"},
    {MimeType::TextCss, "text/css"}, {MimeType::TextJavascript, "text/javascript"},
    {MimeType::TextMarkdown, "text/markdown"}, {MimeType::TextCsv, "text/csv"},
    {MimeType::TextXml, "text/xml"},

    {MimeType::ImagePng, "image/png"}, {MimeType::ImageJpeg, "image/jpeg"},
    {MimeType::ImageGif, "image/gif"}, {MimeType::ImageWebp, "image/webp"},
    {MimeType::ImageBmp, "image/bmp"}, {MimeType::ImageTiff, "image/tiff"},
    {MimeType::ImageSvg, "image/svg+xml"},

    {MimeType::ApplicationPdf, "application/pdf"}, {MimeType::ApplicationZip, "application/zip"},
    {MimeType::ApplicationGzip, "application/gzip"}, {MimeType::ApplicationJson, "application/json"},
    {MimeType::ApplicationXml, "application/xml"}, {MimeType::ApplicationWasm, "application/wasm"},
    {MimeType::ApplicationOctetStream, "application/octet-stream"},
    {MimeType::ApplicationMsword, "application/msword"},
    {MimeType::ApplicationVndExcel, "application/vnd.ms-excel"},
    {MimeType::ApplicationVndPowerpoint, "application/vnd.ms-powerpoint"},
    {MimeType::ApplicationRtf, "application/rtf"}, {MimeType::ApplicationXhtml, "application/xhtml+xml"},

    {MimeType::AudioMpeg, "audio/mpeg"}, {MimeType::AudioOgg, "audio/ogg"},
    {MimeType::AudioWav, "audio/wav"}, {MimeType::AudioWebm, "audio/webm"},
    {MimeType::AudioAac, "audio/aac"}, {MimeType::AudioFlac, "audio/flac"},

    {MimeType::VideoMp4, "video/mp4"}, {MimeType::VideoWebm, "video/webm"},
    {MimeType::VideoOgg, "video/ogg"}, {MimeType::VideoMpeg, "video/mpeg"},
    {MimeType::VideoAvi, "video/x-msvideo"}, {MimeType::VideoMov, "video/quicktime"}
};

MimeType MimeDetector::fromExtension(const std::string &extRaw)
{
    std::string ext = extRaw;
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto it = extensionMap.find(ext);
    return it != extensionMap.end() ? it->second : MimeType::Unknown;
}

MimeType MimeDetector::detectByExtension(const std::string &filename)
{
    auto dot = filename.find_last_of('.');
    if (dot != std::string::npos)
        return fromExtension(filename.substr(dot + 1));
    return MimeType::Unknown;
}

MimeType MimeDetector::detectByContent(const uint8_t *data, size_t size)
{
    if (!data || size < 4) return MimeType::Unknown;

    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return MimeType::ImagePng;
    if (data[0] == 0xFF && data[1] == 0xD8) return MimeType::ImageJpeg;
    if (std::memcmp(data, "GIF8", 4) == 0) return MimeType::ImageGif;
    if (std::memcmp(data, "BM", 2) == 0) return MimeType::ImageBmp;
    if (std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0) return MimeType::ImageWebp;
    if (std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WAVE", 4) == 0) return MimeType::AudioWav;
    if (std::memcmp(data, "OggS", 4) == 0) return MimeType::AudioOgg;
    if (std::memcmp(data, "ID3", 3) == 0 || (data[0] == 0xFF && (data[1] & 0xE0))) return MimeType::AudioMpeg;
    if (std::memcmp(data + 4, "ftyp", 4) == 0) return MimeType::VideoMp4;
    if (std::memcmp(data, "\x1A\x45\xDF\xA3", 4) == 0) return MimeType::VideoWebm;
    if (std::memcmp(data, "PK", 2) == 0) return MimeType::ApplicationZip;
    if (std::memcmp(data, "\x1F\x8B", 2) == 0) return MimeType::ApplicationGzip;
    if (std::memcmp(data, "%PDF", 4) == 0) return MimeType::ApplicationPdf;
    if (std::memcmp(data, "\0asm", 4) == 0) return MimeType::ApplicationWasm;
    if (data[0] == '{' || data[0] == '[') return MimeType::ApplicationJson;

    return MimeType::Unknown;
}

MimeType MimeDetector::detect(const uint8_t *data, size_t size, const std::string &filename)
{
    MimeType type = detectByContent(data, size);
    if (type != MimeType::Unknown) return type;
    return detectByExtension(filename);
}

std::string MimeDetector::toString(MimeType type)
{
    auto it = mimeToString.find(type);
    return it != mimeToString.end() ? it->second : "application/octet-stream";
}

MimeType MimeDetector::fromString(const std::string &mimeStr)
{
    for (const auto &pair: mimeToString) {
        if (pair.second == mimeStr) return pair.first;
    }
    return MimeType::Unknown;
}

std::string MimeDetector::extensionFromType(MimeType type)
{
    for (const auto &pair: extensionMap) {
        if (pair.second == type) return pair.first;
    }
    return "bin";
}
