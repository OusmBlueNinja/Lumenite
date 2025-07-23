//
// Created by spenc on 7/22/2025.
//

#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
#include <unordered_map>

enum class MimeType
{
    Unknown,

    TextPlain, TextHtml, TextCss, TextJavascript, TextMarkdown, TextCsv, TextXml,

    ImagePng, ImageJpeg, ImageGif, ImageWebp, ImageBmp, ImageTiff, ImageSvg,

    ApplicationPdf, ApplicationZip, ApplicationGzip, ApplicationJson, ApplicationXml,
    ApplicationWasm, ApplicationOctetStream, ApplicationMsword, ApplicationVndExcel,
    ApplicationVndPowerpoint, ApplicationRtf, ApplicationXhtml,

    AudioMpeg, AudioOgg, AudioWav, AudioWebm, AudioAac, AudioFlac,

    VideoMp4, VideoWebm, VideoOgg, VideoMpeg, VideoAvi, VideoMov
};

class MimeDetector
{
public:
    // Detect MIME from content first, then fallback to extension
    static MimeType detect(const uint8_t *data, size_t size, const std::string &filename = "");

    // Detect MIME using only the file contents
    static MimeType detectByContent(const uint8_t *data, size_t size);

    // Detect MIME using only the filename extension
    static MimeType detectByExtension(const std::string &filename);

    // Convert MimeType enum to string (e.g. "image/png")
    static std::string toString(MimeType type);

    // Convert MIME string (e.g. "image/png") to enum
    static MimeType fromString(const std::string &mimeStr);

    // Convert extension (e.g. "png") to MIME type
    static MimeType fromExtension(const std::string &ext);

    // Convert MIME type to preferred extension (e.g. "png")
    static std::string extensionFromType(MimeType type);
};
