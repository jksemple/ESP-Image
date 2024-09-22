#include "esp_image.h"

const char* imageTypeName[IMAGE_MAX] = {
    "None",
    "JPEG",
    "RGB565",
    "RGB888",
    "Grayscale8",
    "BMP"
};

const char* pixFormat[9] = {
    "RGB565",
    "YUV422",
    "YUV420",
    "GRAYSCALE",
    "JPEG",
    "RGB888",
    "RAW",
    "RGB444",
    "RGB555"
};
uint8_t jpg_sig[] = { 0xFF, 0xD8};
uint8_t bmp_sig[] = { 0x42, 0x4D};

static void *_malloc(size_t size, const char* FILE, const int LINE)
{
    // check if SPIRAM is enabled and allocate on SPIRAM if allocatable
#if (CONFIG_SPIRAM_SUPPORT && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC))
	auto before = ESP.getFreePsram();
	void* block = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	log_i("PS RAM: %d/%d less %d as %08x-%08x => %d/%d at [%s:%d]", before, ESP.getPsramSize(), size, block, (uint8_t*)block + size-1, ESP.getFreePsram(), ESP.getPsramSize(), FILE, LINE);
	if (block) return block;
	throw std::out_of_range(StringF("[%s:%d] Failed to alloc %d", FILE, LINE, size).c_str());
#else
	throw std::out_of_range(StringF("[%s:%d] No PS RAM", FILE, LINE, size).c_str());
#endif
}

static void _free(void* block, const char* FILE, const int LINE) {
	log_i("Free %x at [%s %d]", block, FILE, LINE);
	std::free(block);
}
// JPG reader
static unsigned int _jpg_read(void * arg, size_t index, uint8_t *buf, size_t len) {
    jpg_decoder * jpeg = (jpg_decoder *)arg;
    if(buf) {
        memcpy(buf, jpeg->input + index, len);
    }
    return len;
}
// JPG size extractor
static bool _jpg_get_size(void* arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t* data) {
    jpg_decoder * jpeg = (jpg_decoder *)arg;
    if(!data){
        if(x == 0 && y == 0){
            jpeg->width = w;
            jpeg->height = h;
        }
        return true;
    } else {
        return false; // Exit early
    }
}
// Need a copy of this from esp32-camera/conversions/to_bmp.c so we can extract the source JPEG size 
// when doing the RGB565 conversion. Also need to alter the byte order to big-endian for compatibility
// with the RGB565 to BMP conversions
static bool _rgb565_write(void * arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data) {
    jpg_decoder * jpeg = (jpg_decoder *)arg;
    if(!data){
        if(x == 0 && y == 0){
            //write start
            jpeg->width = w;
            jpeg->height = h;
            //if output is null, this is BMP
            if(!jpeg->output){
                jpeg->output = (uint8_t *)_malloc((w*h*3)+jpeg->data_offset, __FILE__, __LINE__);
            }
        } else {
            //write end
        }
        return true;
    }

    size_t jw = jpeg->width*3;
    size_t jw2 = jpeg->width*2;
    size_t t = y * jw;
    size_t t2 = y * jw2;
    size_t b = t + (h * jw);
    size_t l = x * 2;
    uint8_t *out = jpeg->output+jpeg->data_offset;
    uint8_t *o = out;
    size_t iy, iy2, ix, ix2;

    w = w * 3;

    for(iy=t, iy2=t2; iy<b; iy+=jw, iy2+=jw2) {
        o = out+iy2+l;
        for(ix2=ix=0; ix<w; ix+= 3, ix2 +=2) {
            uint16_t r = data[ix];
            uint16_t g = data[ix+1];
            uint16_t b = data[ix+2];
            uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            o[ix2] = c>>8;  // Regrettably we need to store in big-endian form in memory while esp-camera driver remains broken
            o[ix2+1] = c&0xff;
        }
        data+=w;
    }
    return true;
}
Image::~Image() { 
    if (buffer) _free(buffer, __FILE__, __LINE__);
}
void Image::setObjectName(String name) {
	if (name.length() == 0) {
		char buffer[10];
		// Set nickname to be address of object
		sprintf(buffer, "%08x", this);
		_objectName = String(buffer);
	} else {
		_objectName = name;
	}
}
bool Image::hasContent() { 
	//log_i("%s %s", _objectName, type != IMAGE_NONE && buffer != NULL ? "has content" : "is empty"); 
    return type != IMAGE_NONE && buffer != NULL; 
}

String Image::typeName() { return imageTypeName[type]; };
Image& Image::fromBuffer(uint8_t* buffer, size_t width, size_t height, size_t len, image_type_t imageType, timeval timestamp) {

	if (buffer == NULL) {
		throw std::invalid_argument(StringF("[%s:%d] %s: Buffer is empty", __FILE__, __LINE__, objectName().c_str()).c_str());
	}
	//log_i("Buffer width=%d height=%d len=%d type=%d", width, height, len, imageType);

	_sourceBuffer = buffer;
	_sourceWidth = width;
	_sourceHeight = height;
	_sourceLen = len;
	_sourceType = imageType;
	_sourceTimestamp = timestamp;
	_from = true;

	return *this;
}

Image& Image::fromCamera(camera_fb_t* frame) {

	// NB Don't believe the camera driver when dealing wih custom image sizes
	// Use setTrueSize() to correct the driver width/height
	if (frame->buf == NULL) {
		throw std::logic_error(StringF("[%s:%d] %s: Nothing captured yet", __FILE__, __LINE__, objectName().c_str()).c_str());
	}
	//log_i("%s: setting _sourceBuffer to %x", objectName(), frame->buf);
	_sourceBuffer = frame->buf;
	_sourceLen = frame->len;
	_sourceWidth = frame->width;
	_sourceHeight = frame->height;
	_sourceTimestamp = frame->timestamp;
	_sourceName = "Camera";
	switch(frame->format) {
		case PIXFORMAT_JPEG:
			_sourceType = IMAGE_JPEG;
			break;
		case PIXFORMAT_RGB565:
		case PIXFORMAT_RGB888:
			if (frame->len == frame->width * frame->height * 2) {
				_sourceType = IMAGE_RGB565;
			} else
			if (frame->len == frame->width * frame->height * 3) {		
				_sourceType = IMAGE_RGB888;
			} else {
				throw std::out_of_range(StringF("[%s:%d] %s: camera frame W=%d H=%d Len=%d Format=%s is inconsistent", __FILE__, __LINE__, objectName().c_str(), frame->width, frame->height, frame->len, imageTypeName[frame->format]).c_str());
			}
			break;
		case PIXFORMAT_GRAYSCALE:
			if (frame->len == frame->width * frame->height) {
				_sourceType = IMAGE_GRAYSCALE8;
			} else {
				throw std::out_of_range(StringF("[%s:%d] %s: camera frame W=%d H=%d Len=%d Format=%s is inconsistent", __FILE__, __LINE__, objectName().c_str(), frame->width, frame->height, frame->len, imageTypeName[frame->format]).c_str());
			}
			break;
		default:
			throw std::invalid_argument(StringF("[%s:%d] %s: Format %s is not currently supported", __FILE__, __LINE__, objectName().c_str(), imageTypeName[frame->format]).c_str());
	}
	_from = true;

	return *this;
}
// Correct the size of a JPEG image by reading the JPEG metadata
// This fixes a problem in the esp-camera driver where custom size images
// requested through sensor->set_res_raw are incorrectly reported in the camera_fb_t buffer
Image& Image::setTrueSize() {
	if (type == IMAGE_JPEG) {
		jpeg.input = buffer;
		esp_jpg_decode(_sourceLen, (jpg_scale_t)_scaling, _jpg_read, _jpg_get_size, (void*)&jpeg);
		width = jpeg.width;
		height = jpeg.height;
		//log_i("Corrected to %d x %d", width, height);
	}
	return *this;
}

Image& Image::fromImage(Image& sourceImage) {

	if (! sourceImage.hasContent()) {
		throw std::invalid_argument(StringF("[%s:%d] %s is empty", __FILE__, __LINE__, sourceImage.objectName().c_str()).c_str());
	}
	_sourceBuffer = sourceImage.buffer;
	_sourceLen = sourceImage.len;
	_sourceWidth = sourceImage.width;
	_sourceHeight = sourceImage.height;
	_sourceType = sourceImage.type;
	_sourceTimestamp = sourceImage.timestamp;
	//log_i("sourceImage.objectName() = %s", sourceImage.objectName());
	_sourceName = sourceImage.objectName();
	_from = true;

	return *this;
}
// Variadic filename formatting
Image& Image::fromFile(FS& fs, const char* format, ...) {
	char buffer[FN_BUF_LEN];
	if (! format) {
		throw std::invalid_argument(StringF("[%s:%d] %s: Missing filename format", __FILE__, __LINE__, objectName().c_str()).c_str());
	} else {
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
	}
	return fromFile(fs, String(buffer));
}
// Infer image type from extension
Image& Image::fromFile(FS& fs, const String& path) {
	String path2 = path;
	path2.toLowerCase();
	if (path2.endsWith(".jpg"))
		return fromFile(fs, path, IMAGE_JPEG);
	else
	if (path2.endsWith(".bmp"))
		return fromFile(fs, path, IMAGE_BMP);
	else
	{
		throw std::invalid_argument(StringF("[%s:%d] %s: Cannot infer image type from %s", __FILE__, __LINE__, objectName().c_str(), path.c_str()).c_str());
	}
}

Image& Image::fromFile(FS& fs, const String& path, image_type_t imageType) {

	_sourceFilename = path;
	_sourceName = path;
	//log_i("_sourceFilename = %s", _sourceFilename);
	_sourceType = imageType;
	_sourceFS = &fs;
	_from = true;
	return *this;
}

void Image::convertTo(image_type_t newImageType, scaling_type_t scaling) {

	log_i("%s: Converting from %s to %s", objectName(), source(), imageTypeName[newImageType]);

	_targetType = newImageType;
	_scaling = scaling;
	if (!_from) {
		_sourceBuffer = buffer;
		_sourceLen = len;
		_sourceType = type;
		_sourceTimestamp = timestamp;
		_sourceWidth = width;
		_sourceHeight = height;
	}
	if (_sourceType == _targetType) {
		throw std::invalid_argument(StringF("[%s:%d] %s: Source and target types are the same", __FILE__, __LINE__, objectName().c_str()).c_str());
	}
	if (_targetType == IMAGE_BMP && _scaling != SCALING_NONE) {
		throw std::invalid_argument(StringF("[%s:%d] %s: Cannot scale when converting to BMP", __FILE__, __LINE__, objectName().c_str()).c_str());
	}
	if (_targetType == IMAGE_JPEG && _scaling != SCALING_NONE) {
		throw std::invalid_argument(StringF("[%s:%d] %s: Cannot scale when converting to JPEG", __FILE__, __LINE__, objectName().c_str()).c_str());
	}
	if (_sourceType == IMAGE_JPEG && _targetType == IMAGE_RGB565) {
		//log_i("SourceW = %d, SourceH = %d", _sourceWidth, _sourceHeight);
		_targetLen = (_sourceWidth >> scaling) * (_sourceHeight >> scaling) * 2;
		_targetBuffer = (uint8_t*)_malloc(_targetLen, __FILE__, __LINE__);
		//log_i("JPG2RGB565 %d %d", _sourceLen, _scaling);
		jpeg.input = _sourceBuffer;
		jpeg.output = _targetBuffer;
		esp_jpg_decode(_sourceLen, (jpg_scale_t)_scaling, _jpg_read, _rgb565_write, (void*)&jpeg);
		_targetWidth = jpeg.width;
		_targetHeight = jpeg.height;
		_targetLen = _targetWidth * _targetHeight * 2;
	} else 
	if (_targetType == IMAGE_BMP) {
		pixformat_t fromType;
		switch (_sourceType) {
			case IMAGE_JPEG:
				fromType = PIXFORMAT_JPEG;
				break;
			case IMAGE_RGB565:
				fromType = PIXFORMAT_RGB565;
				break;
			case IMAGE_GRAYSCALE8:
				fromType = PIXFORMAT_GRAYSCALE;
				break;
			default:
				throw std::invalid_argument(StringF("[%s:%d] %s: Cannot convert to BMP from %s", __FILE__, __LINE__, objectName().c_str(), imageTypeName[_sourceType]).c_str());
		}

		//log_i("Running fmt2bmp");
		if (!fmt2bmp(_sourceBuffer, _sourceLen, _sourceWidth, _sourceHeight, fromType, &_targetBuffer, &_targetLen)) {
			if (_targetBuffer) std::free(_targetBuffer);
			_targetBuffer = 0;
			throw std::invalid_argument(StringF("[%s:%d] fmt2bmp failed", __FILE__, __LINE__).c_str());
		} else {
			//log_i("%s: _targetBuffer = %x (%d)", objectName(), _targetBuffer, _targetLen);
			_targetWidth = _sourceWidth;
			_targetHeight = _sourceHeight;
			_targetTimestamp = _sourceTimestamp;
		}
	} else
	if (_targetType == IMAGE_JPEG) {
		pixformat_t fromPixFormat;
		
		switch (_sourceType) {
			case IMAGE_RGB888:
			case IMAGE_BMP:
				if (_sourceType == IMAGE_BMP) _sourceBuffer += BMP_HEADER_LEN;
				if (!fmt2jpg(_sourceBuffer, _sourceLen, _sourceWidth, _sourceHeight, PIXFORMAT_RGB888, 12, &_targetBuffer, &_targetLen)) {
					if (_targetBuffer) _free(_targetBuffer, __FILE__, __LINE__);
					_targetBuffer = 0;
					throw std::invalid_argument(StringF("[%s:%d] fmt2bmp failed", __FILE__, __LINE__).c_str());
				}
				_targetWidth = _sourceWidth;
				_targetHeight = _sourceHeight;
				_targetTimestamp = _sourceTimestamp;
				break;
			case IMAGE_RGB565:
				if (!fmt2jpg(_sourceBuffer, _sourceLen, _sourceWidth, _sourceHeight, PIXFORMAT_RGB565, 12, &_targetBuffer, &_targetLen)) {
					if (_targetBuffer) _free(_targetBuffer, __FILE__, __LINE__);
					_targetBuffer = 0;
					throw std::invalid_argument(StringF("[%s:%d] fmt2bmp failed", __FILE__, __LINE__).c_str());
				}
//				log_i("Written to %08x (%d)", _targetBuffer, _targetLen);
				_targetWidth = _sourceWidth;
				_targetHeight = _sourceHeight;
				_targetTimestamp = _sourceTimestamp;
				break;
			case IMAGE_GRAYSCALE8:
				if (!fmt2jpg(_sourceBuffer, _sourceLen, _sourceWidth, _sourceHeight, PIXFORMAT_GRAYSCALE, 12, &_targetBuffer, &_targetLen)) {
					if (_targetBuffer) _free(_targetBuffer, __FILE__, __LINE__);
					_targetBuffer = 0;
					throw std::invalid_argument(StringF("[%s:%d] fmt2bmp failed", __FILE__, __LINE__).c_str());
				}
				_targetWidth = _sourceWidth;
				_targetHeight = _sourceHeight;
				_targetTimestamp = _sourceTimestamp;
				break;
			default:
				throw std::invalid_argument(StringF("[%s:%d] %s: Cannot convert to JPEG from %s", __FILE__, __LINE__, objectName().c_str(), imageTypeName[_sourceType]).c_str());
		}
	}
	//log_i("%s: Buffer is %08x", objectName(), buffer);
	if (buffer) _free(buffer, __FILE__, __LINE__);
	//log_i("%s: Setting buffer to %08x", objectName(), _targetBuffer);
	buffer = _targetBuffer;
	_targetBuffer = 0;
	len = _targetLen;
	type = _targetType;
	width = _targetWidth;
	height = _targetHeight;
	timestamp = _targetTimestamp;
	log_i("%s: converted to %s (%d x %d) from %s", objectName(), typeName(), width, height, source());
	return;
}

// Just load an image from a source without any conversion or scaling
void Image::load(missing_file_on_load_t missing_file_option) {
	if (! _from) {
		throw std::invalid_argument(StringF("[%s:%d] Missing fromXXX() clause", __FILE__, __LINE__).c_str());
	}
	log_i("%s: Load from %s", objectName(), source().c_str());
	
	if (_sourceFilename == "") {
		_targetLen = _sourceLen;
		_targetBuffer = (uint8_t*)_malloc(_targetLen, __FILE__, __LINE__);
		memcpy(_targetBuffer, _sourceBuffer, _sourceLen);
		_targetWidth = _sourceWidth;
		_targetHeight = _sourceHeight;
		_targetType = _sourceType;
		_targetTimestamp = _sourceTimestamp;
	} else {
		size_t bytesRead = 0;
		File file;
		if (!_sourceFS->exists(_sourceFilename)) {
			if (missing_file_option == IGNORE_MISSING_FILE) {
				log_e("%s: Missing file %s", objectName().c_str(), _sourceFilename.c_str());
				clear();
				return;
			} else {
			throw std::invalid_argument(StringF("[%s:%d] Missing file %s", __FILE__, __LINE__, _sourceFilename.c_str()).c_str());
			}
		}
		file = _sourceFS->open(_sourceFilename, FILE_READ);
		
		size_t fileSize = file.size();
		//log_i("File size of %s = %d", _sourceFilename, fileSize);
		_targetBuffer = (uint8_t*)_malloc(fileSize, __FILE__, __LINE__);
		bytesRead = file.readBytes((char*)_targetBuffer, fileSize);
		if (bytesRead != fileSize) {
			_free(_targetBuffer, __FILE__, __LINE__);
			_targetBuffer = 0;
			throw std::runtime_error(StringF("[%s:%d] Incomplete file read from %s", __FILE__, __LINE__, _sourceFilename.c_str()).c_str());
		}
		_sourceName = _sourceFilename;
		_targetLen = fileSize;

		_targetType = _sourceType;
		//log_i("Target type %d", _targetType);
		switch(_targetType) {
			case IMAGE_JPEG:
				if (! (_targetBuffer[0] == jpg_sig[0] && _targetBuffer[1] == jpg_sig[1])) {
					_free(_targetBuffer, __FILE__, __LINE__);
					_targetBuffer = 0;
					throw std::invalid_argument(StringF("[%s:%d] %s: contents of %s are not %s", __FILE__, __LINE__, objectName().c_str(), _sourceFilename.c_str(), imageTypeName[_targetType]).c_str());	
				}
				jpeg.input = _targetBuffer;
				//log_i("Starting esp_jpg_decode");
				esp_jpg_decode(_targetLen, JPG_SCALE_NONE, _jpg_read, _jpg_get_size, (void*)&jpeg);
				//log_i("Done");
				_targetWidth = jpeg.width;
				_targetHeight = jpeg.height;
				_targetTimestamp.tv_sec = file.getLastWrite();
				_targetTimestamp.tv_usec = 0;
				break;
			case IMAGE_BMP:
				if (! (_targetBuffer[0] == bmp_sig[0] && _targetBuffer[1] == bmp_sig[1])) {
					_free(_targetBuffer, __FILE__, __LINE__);
					_targetBuffer = 0;
					throw std::invalid_argument(StringF("[%s:%d] %s: contents of %s are not %s", __FILE__, __LINE__, objectName().c_str(), _sourceFilename.c_str(), imageTypeName[_targetType]).c_str());	
				}
				_targetWidth = *(uint32_t*)(_targetBuffer + BMP_WIDTH_ADDR);
				_targetHeight = *(uint32_t*)(_targetBuffer + BMP_HEIGHT_ADDR);
				_targetTimestamp.tv_sec = file.getLastWrite();
				_targetTimestamp.tv_usec = 0;
				break;
			default:
				_free(_targetBuffer, __FILE__, __LINE__);
				_targetBuffer = 0;
				throw std::invalid_argument(StringF("[%s:%d] %s: cannot load %s", __FILE__, __LINE__, objectName().c_str(), imageTypeName[_targetType]).c_str());
		}
	}
	// Replace previous image content if any
	//log_i("%s: Buffer is %08x", objectName(), buffer);
	if (buffer) _free(buffer, __FILE__, __LINE__);
	//log_i("%s: Setting buffer to %08x", objectName(), _targetBuffer);
	buffer = _targetBuffer;
	_targetBuffer = 0;
	len = _targetLen;
	width = _targetWidth;
	height = _targetHeight;
	type = _targetType;
	timestamp = _targetTimestamp;
	log_i("%s: loaded %s (%d x %d) from %s", objectName().c_str(), typeName(), width, height, source());
 	return;
}

// Variadic filename formatting
Image& Image::toFile(FS& fs, const char* format, ...) {
	char buffer[FN_BUF_LEN];
	if (! format) {
		throw std::invalid_argument(StringF("[%s:%d] %s:Missing filename format", __FILE__, __LINE__, objectName().c_str()).c_str());
	} else {
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
	}
	return toFile(fs, String(buffer));
}

Image& Image::toFile(FS& fs, const String& filename) {
	File file;
	bool ret = false;
	if (!filename.startsWith("/")) {
		_targetFilename = String("/") + filename;
	} else {
		_targetFilename = filename;
	}
	if (_targetFilename.indexOf("//") >= 0) {
		_targetFilename.replace("//", "/");
	}

	_targetFS = &fs;
	_to = true;
	
	return *this;
}

void Image::save(existing_file_on_save_t existing_file_option) {
	if (! _to) {
		throw std::invalid_argument(StringF("[%s:%d] %s:Missing toFile() clause", __FILE__, __LINE__, objectName().c_str()).c_str());
	}
	if (_targetFS->exists(_targetFilename) ) {
		if (existing_file_option == OVERWRITE_EXISTING_FILE) {
			log_i("%s: Overwriting existing file %s", objectName(), _targetFilename);
		} else {
			throw std::invalid_argument(StringF("[%s:%d] %s:File %s exists", __FILE__, __LINE__, objectName().c_str(), _targetFilename.c_str()).c_str());
		}
	}
	File file = _targetFS->open(_targetFilename, FILE_WRITE);
	//log_d("Starting to write %s\n", path);
	unsigned long start = millis();
	if (!file) {
		log_e("File open failed %s", _targetFilename);
		throw std::invalid_argument(StringF("[%s:%d] %s:Invalid filename %s", __FILE__, __LINE__, objectName().c_str(), _targetFilename.c_str()).c_str());
	} else {

		if (file.write(buffer, len) != len) {
			log_e("File write failed %s", _targetFilename);
			throw std::runtime_error(StringF("[%s:%d] %s:Incomplete file write to %s", __FILE__, __LINE__, objectName().c_str(), _targetFilename.c_str()).c_str());
		} else {
		//  log_d("Finished writing to %s\n", path);
		}
		file.close();

		log_i("%s: Wrote file %s in %d ms", objectName().c_str(), _targetFilename, (int)(millis() - start));
		return;
	}
}

void Image::setPixel(int x, int y, int r, int g, int b) {
	if (x < 0 || x >= width) {
		log_e("%s: x=0 > %d > %d", objectName().c_str(), x, width);
		return;
	}
	if (y < 0 || y >= height) {
		log_e("%s: y=0 > %d > %d", objectName().c_str(), y, height);
		return;
	}
	if (type == IMAGE_RGB565) {
		uint16_t pixelVal = r << 11 | g << 5 | b;
		uint8_t h = pixelVal >> 8;
		uint8_t l = pixelVal & 0xFF;
		buffer[2 * (y * width + x)] = h; // Store in big-endian form for compatibility with the majority of the broken esp-camera driver
		buffer[2 * (y * width + x) + 1] = l;
		return;
	} else 
	if (type == IMAGE_RGB888) {
		buffer[3 * (y * width + x)] = b;
		buffer[3 * (y * width + x) + 1] = g;
		buffer[3 * (y * width + x) + 2] = r;
		return;
	} else {
		throw std::invalid_argument(StringF("[%s:%d] %s: Cannot setPixel for %s", __FILE__, __LINE__, objectName().c_str(), typeName().c_str()).c_str());
	}
}

int Image::greyAt(int x, int y) {
	if (x < 0 || x > width || y < 0 || y > height) {
		throw std::invalid_argument(StringF("[%s:%d] %s: %d, %d is out of bounds", __FILE__, __LINE__, objectName().c_str(), x, y).c_str());
	}
	return pixelAt(x, y).grey();
}

Pixel Image::pixelAt(int x, int y) {
	if (x < 0 || x >= width || y < 0 || y >= height) {
		throw std::invalid_argument(StringF("[%s:%d] %s:%d, %d is out of bounds", __FILE__, __LINE__, objectName().c_str(), x, y).c_str());
	}
	if (type == IMAGE_RGB565) {
		auto pixBuf = (uint16_t*)buffer;
		return Pixel(pixBuf[y * width + x]);
	} else
	if (type == IMAGE_RGB888) {
		uint8_t* ppixel = buffer + 3 * (y * width + x);
		return Pixel(*(ppixel + 2), *(ppixel + 1), *ppixel); // Stored as B G R in memory
	} else 
	if (type == IMAGE_BMP) {
		uint8_t* ppixel = buffer + 3 * (y * width + x) + BMP_HEADER_LEN; 
		return Pixel(*(ppixel + 2), *(ppixel + 1), *ppixel); // Stored as B G R in memory
	} else {
		throw std::invalid_argument(StringF("[%s:%d] %s: Cannot get pixelAt() for %s", __FILE__, __LINE__, objectName().c_str(), typeName().c_str()).c_str());
	}
}
// Compare this image with another similar one
// The comparisonFunction can be a lambda or some other form of std::function but it must:
// - accept an x and y position of Pixels being compared
// - accept the Pixel values at those coords from both images
// - return a true/false value that indicates if the compared Pixels differ in some arbitrary way
//   by more than a threshold value.  False indicates no significant difference
// The compareWith() method returns a float being the number of such differing pixels divided by the number of pixels checked.
float Image::compareWith(Image& that, int stride, comparisonFunction func) {
	if (width != that.width && height != that.height) {
		throw std::invalid_argument(StringF("[%s:%d] %s and %s are not the same size", __FILE__, __LINE__, objectName().c_str(), that.objectName().c_str()).c_str());
	}
	if (type != IMAGE_RGB565 || that.type != IMAGE_RGB565) {
		throw std::invalid_argument(StringF("[%s:%d] %s and %s are not the same type", __FILE__, __LINE__, objectName().c_str(), that.objectName().c_str()).c_str());
	}
	if (stride < 1) {
		throw std::invalid_argument(StringF("[%s:%d] %s: Stride must be 1 or more", __FILE__, __LINE__, objectName().c_str()).c_str());
	}
	int comparedCount = 0;
	int diffCount = 0;
	for (int y = 0; y < height; y += stride) {
		for (int x = 0; x < width; x += stride) {
			comparedCount ++;
			diffCount += func(x, y, pixelAt(x, y), that.pixelAt(x, y)) ? 1 : 0;
		}
	}
	return (float)diffCount / comparedCount;
}

void Image::clear() {
	if (buffer) {
		_free(buffer, __FILE__, __LINE__);
		buffer = 0;
	}
	len = 0;
	_sourceName = "";
	width = 0;
	height = 0;
	type = IMAGE_NONE;
	timestamp = { 0, 0 };
	_from = false;
}

