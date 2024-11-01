#ifndef ESP_IMAGE_H
#define ESP_IMAGE_H
#include "img_converters.h"
#include <FS.h>
#include "string.h"
#include "StringF.h"
#include "AppException.h"
#include "map"
#include "vector"

typedef enum {
    IMAGE_NONE,
    IMAGE_JPEG,
    IMAGE_RGB565,
    IMAGE_RGB888,
    IMAGE_GRAYSCALE8,
    IMAGE_BMP,
    IMAGE_MAX
} image_type_t;

typedef enum {
    SCALING_NONE,
    SCALING_DIVIDE_2,
    SCALING_DIVIDE_4,
    SCALING_DIVIDE_8,
    SCALING_DIVIDE_16,
    SCALING_DIVIDE_32
} scaling_type_t;

typedef struct {
        uint16_t width;
        uint16_t height;
        uint16_t data_offset;
        const uint8_t *input;
        uint8_t *output;
} jpg_decoder;

typedef enum {
    IGNORE_MISSING_IMAGE_FILE,
    THROW_IF_MISSING_IMAGE
} missing_image_file_on_load_t;

typedef enum {
    OVERWRITE_EXISTING_IMAGE_FILE,
    THROW_IF_IMAGE_EXISTS
} existing_image_file_on_save_t;

typedef struct {
    uint32_t filesize;
    uint32_t reserved;
    uint32_t fileoffset_to_pixelarray;
    uint32_t dibheadersize;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bitsperpixel;
    uint32_t compression;
    uint32_t imagesize;
    uint32_t ypixelpermeter;
    uint32_t xpixelpermeter;
    uint32_t numcolorspallette;
    uint32_t mostimpcolor;
} bmp_header_t;

static const int BMP_HEADER_LEN = 54;

// Windows BMP format
#define BMP_WIDTH_ADDR 0x12
#define BMP_HEIGHT_ADDR 0x16
#define BMP_BPP_ADDR 0x1C
#define FN_BUF_LEN 60

// JPEG stuff

const uint16_t SOI = 0xD8FF; // FFD8
const uint16_t SOF0 = 0xC0FF; // FFC0
const uint16_t SOS = 0xDAFF; // FFDA
const uint16_t APP0 = 0xE0FF; // FFE0
const uint16_t APP1 = 0xE1FF; // FFE1 for EXIF
const uint16_t EOI = 0xD9FF; // FFD9

typedef struct {
    uint16_t marker;
    uint8_t bigendianOffset[2];
} jpeg_segment_t;

typedef struct {
    uint8_t bitsPerPixel;
    uint8_t imageHeight[2];
    uint8_t imageWidth[2];
} sof0_segment_t;

class Pixel {
    public:
        Pixel(int r, int g, int b) :
            r(r),
            g(g),
            b(b) {
        };
        Pixel(uint16_t rgb565Value) {
            // To minimize alterations to the broken esp-camera driver we will have to accept
            // that RGB565 values will be stored in high-endian form in memory
            uint16_t lowEndianValue = (rgb565Value & 0xFF00) >> 8 | (rgb565Value & 0xFF) << 8;
            r = (lowEndianValue & 0xF800) >> 8;
            g = (lowEndianValue & 0x07E0) >> 3;
            b = (lowEndianValue & 0x001F) << 3;
        }
        uint8_t r;
        uint8_t g;
        uint8_t b;
        ~Pixel() {};
        uint8_t grey() {
            // R = 306 / 1024 = 0.299
            // G = 600 / 1024 = 0.586
            // B = 117 / 1024 = 0.114
            uint32_t grey = ((uint32_t)r * 306 + (uint32_t)g * 600 + (uint32_t)b * 117) >> 10;
            return grey;
        }
};

typedef std::function <bool(int x, int y, Pixel thisPixel, Pixel thatPixel)> comparisonFunction;
typedef std::function <bool(int x, int y, int width, int height)> maskFunction;

typedef std::function <void(int x, int y, Pixel pixel)> actionFunction;

// Predefined mask functions

bool noMask(int x, int y, int width, int height);
bool insideCircle(int x, int y, int width, int height);
bool outsideCircle(int x, int y, int width, int height);
bool insideCentralCircle(int x, int y, int width, int height);

class Image {
    public:
        Image() : Image("") {};
        Image(const char* objectName) : 
            type(IMAGE_NONE),
            buffer(nullptr),
            width(0),
            height(0),
            len(0),
            timestamp({ 0, 0}),
            _sourceName("") {
                setObjectName(objectName);
            };
        ~Image();
        bool hasContent();
        image_type_t type;
        String typeName();
        uint8_t* buffer;
        size_t len;
        uint16_t width;
        uint16_t height;
        struct timeval timestamp;
        String source() { return _sourceName; }
        String objectName() { return _objectName; };
        std::map<String, String> metadata;
    private:
        String _objectName;
        String _sourceName;
        String _sourceFilename;
        uint8_t* _sourceBuffer;
        size_t _sourceLen;
        uint16_t _sourceWidth;
        uint16_t _sourceHeight;
        image_type_t _sourceType;
        timeval _sourceTimestamp;
        std::map<String, String>* _sourceMetadataPtr;
        uint8_t* _targetBuffer;
        size_t _targetLen;
        uint16_t _targetWidth;
        uint16_t _targetHeight;
        image_type_t _targetType;
        timeval _targetTimestamp;
        std::map<String, String>* _targetMetadataPtr;
        FS* _sourceFS;
        bool _from = false;
        FS* _targetFS;
        String _targetFilename;
        bool _to = false;
        scaling_type_t _scaling;
        jpg_decoder jpeg;
        String readFileToChar(File& file, char endChar);
        std::vector<String> split(const String& s, char splitChar);

    public:
        Image& fromBuffer(uint8_t* buffer, size_t width, size_t height, size_t len, image_type_t imageType, timeval timestamp = { 0, 0 });
        Image& fromCamera(camera_fb_t* frame);
        uint16_t bigEndianWord(const uint8_t* ptr);
        uint32_t bigEndianLong(const uint8_t* ptr);
        const uint8_t* nextJpegSegment(const uint8_t* startPtr, const uint8_t* endPtr);
        void extractJpegSize(uint16_t& width, uint16_t& height, uint8_t* buffer, size_t len);
        Image& setTrueSize();
        Image& fromImage(Image& sourceImage);
        Image& fromFile(FS& fs, const char* format, ...);
        Image& fromFile(FS& fs, const String& path);
        Image& fromFile(FS& fs, const String& path, image_type_t imageType);
        Image& toFile(FS& fs, const char* format, ...);
        Image& toFile(FS& fs, const String& path);
        void convertTo(image_type_t newImageType) { return convertTo(newImageType, SCALING_NONE); }
        void convertTo(image_type_t newImageType, scaling_type_t scaling);
        void load(missing_image_file_on_load_t = IGNORE_MISSING_IMAGE_FILE);
        void save(existing_image_file_on_save_t = OVERWRITE_EXISTING_IMAGE_FILE);
        void setObjectName(String name);
        void setPixel(int x, int y, int r, int g, int b);
        int greyAt(int x, int y);
        int maxGrey(maskFunction maskFunc = nullptr);
        int minGrey(maskFunction maskFunc = nullptr);
        Pixel pixelAt(int x, int y);
        float compareWith(Image& that, comparisonFunction cFunc) { return compareWith(that, 1, cFunc, noMask);}
        float compareWith(Image& that, int stride, comparisonFunction cFunc) { return compareWith(that, stride, cFunc, noMask); }
        float compareWith(Image& that, comparisonFunction cFunc, maskFunction mFunc) { return compareWith(that, 1, cFunc, mFunc); }
        float compareWith(Image& that, int stride, comparisonFunction func, maskFunction mFunc);
        void foreachPixel(maskFunction mFunc, actionFunction aFunc);
        void clear();
};
#endif