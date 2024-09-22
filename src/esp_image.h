#include "img_converters.h"
#include <dl_image.hpp>
#include <FS.h>
#include "string.h"
#include "StringF.h"

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
    IGNORE_MISSING_FILE,
    THROW_IF_MISSING
} missing_file_on_load_t;

typedef enum {
    OVERWRITE_EXISTING_FILE,
    THROW_IF_EXISTS
} existing_file_on_save_t;

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
#define TAG ""

// Windows BMP format
#define BMP_WIDTH_ADDR 0x12
#define BMP_HEIGHT_ADDR 0x16
#define BMP_BPP_ADDR 0x1C
#define FN_BUF_LEN 60

//static void _free(void* block, const char* FILE, const int LINE);
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

class Image {
    public:
        Image() : Image("") {};
        Image(const char* objectName) : 
            type(IMAGE_NONE),
            buffer(NULL),
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
        size_t width;
        size_t height;
        struct timeval timestamp;
        String source() { return _sourceName; }
        String objectName() { return _objectName; };
    private:
        String _objectName;
        String _sourceName;
        uint8_t* _sourceBuffer;
        size_t _sourceLen;
        size_t _sourceWidth;
        size_t _sourceHeight;
        image_type_t _sourceType;
        timeval _sourceTimestamp;
        uint8_t* _targetBuffer;
        size_t _targetLen;
        size_t _targetWidth;
        size_t _targetHeight;
        image_type_t _targetType;
        timeval _targetTimestamp;
        String _sourceFilename;
        FS* _sourceFS;
        bool _from = false;
        FS* _targetFS;
        String _targetFilename;
        bool _to = false;
        scaling_type_t _scaling;
        jpg_decoder jpeg;

    public:
        Image& fromBuffer(uint8_t* buffer, size_t width, size_t height, size_t len, image_type_t imageType, timeval timestamp = { 0, 0 });
        Image& fromCamera(camera_fb_t* frame);
        Image& setTrueSize();
        Image& fromImage(Image& sourceImage);
        Image& fromFile(FS& fs, const char* format, ...);
        Image& fromFile(FS& fs, const String& path);
        Image& fromFile(FS& fs, const String& path, image_type_t imageType);
        Image& toFile(FS& fs, const char* format, ...);
        Image& toFile(FS& fs, const String& path);
        void convertTo(image_type_t newImageType) { return convertTo(newImageType, SCALING_NONE); }
        void convertTo(image_type_t newImageType, scaling_type_t scaling);
        void load(missing_file_on_load_t = IGNORE_MISSING_FILE);
        void save(existing_file_on_save_t = OVERWRITE_EXISTING_FILE);
        void setObjectName(String name);
        void setPixel(int x, int y, int r, int g, int b);
        int greyAt(int x, int y);
        Pixel pixelAt(int x, int y);
        typedef std::function <bool(int x, int y, Pixel thisPixel, Pixel thatPixel)> comparisonFunction;
        float compareWith(Image& that, comparisonFunction func) { return compareWith(that, 1, func); }
        float compareWith(Image& that, int stride, comparisonFunction func);
        void clear();
};
