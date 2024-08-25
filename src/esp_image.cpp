#include "esp_image.h"

const char* imageTypeName[IMAGE_MAX] = {
    "None"
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
// when doing the RGB565 conversion 
static bool _rgb565_write(void * arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data) {
    jpg_decoder * jpeg = (jpg_decoder *)arg;
    if(!data){
        if(x == 0 && y == 0){
            //write start
            jpeg->width = w;
            jpeg->height = h;
            //if output is null, this is BMP
            if(!jpeg->output){
                jpeg->output = (uint8_t *)ps_malloc((w*h*3)+jpeg->data_offset);
                if(!jpeg->output){
                    return false;
                }
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
            o[ix2+1] = c>>8;
            o[ix2] = c&0xff;
        }
        data+=w;
    }
    return true;
}

bool fmt2bmpFixed(uint8_t *src, size_t src_len, uint16_t width, uint16_t height, pixformat_t format, uint8_t ** out, size_t * out_len) {
    //if(format == PIXFORMAT_JPEG) {
    //    return jpg2bmp(src, src_len, out, out_len);
    //}

    *out = NULL;
    *out_len = 0;

    int pix_count = width*height;

    // With BMP, 8-bit greyscale requires a palette.
    // For a 640x480 image though, that's a savings
    // over going RGB-24.
    int bpp = (format == PIXFORMAT_GRAYSCALE) ? 1 : 3;
    int palette_size = (format == PIXFORMAT_GRAYSCALE) ? 4 * 256 : 0;
    size_t out_size = (pix_count * bpp) + BMP_HEADER_LEN + palette_size;
    uint8_t * out_buf = (uint8_t *)ps_malloc(out_size);
    if(!out_buf) {
        ESP_LOGE(TAG, "_malloc failed! %u", out_size);
        return false;
    }

    out_buf[0] = 'B';
    out_buf[1] = 'M';
    bmp_header_t * bitmap  = (bmp_header_t*)&out_buf[2];
    bitmap->reserved = 0;
    bitmap->filesize = out_size;
    bitmap->fileoffset_to_pixelarray = BMP_HEADER_LEN + palette_size;
    bitmap->dibheadersize = 40;
    bitmap->width = width;
    bitmap->height = -height;//set negative for top to bottom
    bitmap->planes = 1;
    bitmap->bitsperpixel = bpp * 8;
    bitmap->compression = 0;
    bitmap->imagesize = pix_count * bpp;
    bitmap->ypixelpermeter = 0x0B13 ; //2835 , 72 DPI
    bitmap->xpixelpermeter = 0x0B13 ; //2835 , 72 DPI
    bitmap->numcolorspallette = 0;
    bitmap->mostimpcolor = 0;

    uint8_t * palette_buf = out_buf + BMP_HEADER_LEN;
    uint8_t * pix_buf = palette_buf + palette_size;
    uint8_t * src_buf = src;

    if (palette_size > 0) {
        // Grayscale palette
        for (int i = 0; i < 256; ++i) {
            for (int j = 0; j < 3; ++j) {
                *palette_buf = i;
                palette_buf++;
            }
            // Reserved / alpha channel.
            *palette_buf = 0;
            palette_buf++;
        }
    }

    //convert data to RGB888
    if(format == PIXFORMAT_RGB888) {
        memcpy(pix_buf, src_buf, pix_count*3);
    } else if(format == PIXFORMAT_RGB565) {
        int i;
        uint8_t hb, lb;
        for(i=0; i<pix_count; i++) {
            lb = *src_buf++; // These lines have been
            hb = *src_buf++; // switched from the original code
            *pix_buf++ = (lb & 0x1F) << 3;
            *pix_buf++ = (hb & 0x07) << 5 | (lb & 0xE0) >> 3;
            *pix_buf++ = hb & 0xF8;
        }
    } else if(format == PIXFORMAT_GRAYSCALE) {
        memcpy(pix_buf, src_buf, pix_count);
    } 
    *out = out_buf;
    *out_len = out_size;
    return true;
}

bool Image::hasContent() { 
            log_i("%x %s", this, type != IMAGE_NONE && buffer != NULL ? "has content" : "is empty"); 
            return type != IMAGE_NONE && buffer != NULL; 
}

Image& Image::fromBuffer(uint8_t* buffer, size_t width, size_t height, size_t len, image_type_t imageType, timeval timestamp) {

	if (buffer == NULL) {
		error.set("Buffer is empty");
		return *this;
	}
	log_i("Width=%d height=%d len=%d type=%d", width, height, len, imageType);

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
		error.set("No captured image");
		return *this;
	}
	_sourceBuffer = frame->buf;
	_sourceLen = frame->len;
	_sourceWidth = frame->width;
	_sourceHeight = frame->height;
	_sourceTimestamp = frame->timestamp;
	switch(frame->format) {
		case PIXFORMAT_JPEG:
			_sourceType = IMAGE_JPEG;
			break;
		case PIXFORMAT_RGB565:
			_sourceType = IMAGE_RGB565;
			break;
		case PIXFORMAT_GRAYSCALE:
			_sourceType = IMAGE_GRAYSCALE8;
			break;
		default:
			error.set(String("Unsupported format ") + pixFormat[frame->format]);
			return *this;
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
		log_i("Corrected to %d x %d", width, height);
	}
	return *this;
}

Image& Image::fromImage(Image& sourceImage) {

	if (! sourceImage.hasContent()) {
		error.set("Image is empty");
		return *this;
	}
	_sourceBuffer = sourceImage.buffer;
	_sourceLen = sourceImage.len;
	_sourceWidth = sourceImage.width;
	_sourceHeight = sourceImage.height;
	_sourceType = sourceImage.type;
	_sourceTimestamp = sourceImage.timestamp;
	_from = true;

	return *this;
}
// Variadic filename formatting
Image& Image::fromFile(FS& fs, const char* format, ...) {
	char buffer[FN_BUF_LEN];
	if (! format) {
		error.set("Missing filename format");
		return *this;
	} else {
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
	}
	return fromFile(fs, String(buffer));
}
// Infer image type from extension
Image& Image::fromFile(FS& fs, String path) {
	String path2 = path;
	path2.toLowerCase();
	if (path2.endsWith(".jpg"))
		return fromFile(fs, path, IMAGE_JPEG);
	else
	if (path2.endsWith(".bmp"))
		return fromFile(fs, path, IMAGE_BMP);
	else
	{
		error.set("Cannot infer image type of " + path);
		return *this;
	}
}

Image& Image::fromFile(FS& fs, String path, image_type_t imageType) {

	if (!fs.exists(path)) {
		error.set(String("Missing file ") + path);
		return *this;
	}
	_sourceFilename = path;
	log_i("_sourceFilename = %s", _sourceFilename);
	_sourceType = imageType;
	_sourceFS = &fs;
	_from = true;
	return *this;
}

Error& Image::convertTo(image_type_t newImageType, scaling_type_t scaling) {
	if (error.toString() != "") {
		log_i("Error = %s", error.toCString());
		return error;
	}
	if (!error.isOK()) {
		log_i("Error = %s", error.toCString());
		return error;
	}

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
		return error.set("source and target types are the same");
	}
	if (_sourceType == IMAGE_JPEG && _targetType == IMAGE_RGB565) {
		log_i("SourceW = %d, SourceH = %d", _sourceWidth, _sourceHeight);
		_targetLen = (_sourceWidth >> scaling) * (_sourceHeight >> scaling) * 2;
		_targetBuffer = (uint8_t*)ps_malloc(_targetLen);
		if (! _targetBuffer)
			return error.set("failed to ps_malloc %d", _targetLen);
		log_i("JPG2RGB565 %d %d", _sourceLen, _scaling);
		jpeg.input = _sourceBuffer;
		jpeg.output = _targetBuffer;
		esp_jpg_decode(_sourceLen, (jpg_scale_t)_scaling, _jpg_read, _rgb565_write, (void*)&jpeg);
		_targetWidth = jpeg.width;
		_targetHeight = jpeg.height;
		_targetLen = _targetWidth * _targetHeight * 2;
	} else 
	if (_targetType == IMAGE_BMP) {
		if (_scaling != SCALING_NONE) {
			return error.set("cannot scale when converting to BMP");
		}
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
				return error.set(String("cannot convert to BMP from ") + imageTypeName[_sourceType]);
		}

		if (!fmt2bmpFixed(_sourceBuffer, _sourceLen, _sourceWidth, _sourceHeight, fromType, &_targetBuffer, &_targetLen)) {
			return error.set("fmt2bmp failed");
		}
	} else
	if (_targetType == IMAGE_JPEG) {
		if (_scaling != SCALING_NONE) {
			return error.set("cannot scale when converting to JPEG");
		}
		pixformat_t fromPixFormat;
		uint8_t* tempBuffer;
		switch (_sourceType) {
			case IMAGE_RGB565:
				fromPixFormat = PIXFORMAT_RGB565;
				// fmt2jpg() assumes RGB565 is in high-byte low-byte order. Until that is fixed we either have to copy all the code in to_jpg.cpp and modify
				// or flip the byte order before calling it. 
				tempBuffer = (uint8_t*)ps_malloc(_sourceLen);
				if (! tempBuffer) {
					return error.set("failed to ps_malloc %d", _sourceLen);
				}
				for (int i = 0; i < _sourceLen; i += 2) {
					tempBuffer[i] = _sourceBuffer[i+1];
					tempBuffer[i+1] = _sourceBuffer[i];
				}
				if (!fmt2jpg(tempBuffer, _sourceLen, _sourceWidth, _sourceHeight, fromPixFormat, 12, &_targetBuffer, &_targetLen)) {
					return error.set("fmt2jpg failed");
				}
				std::free(tempBuffer);
				break;
			case IMAGE_GRAYSCALE8:
				fromPixFormat = PIXFORMAT_GRAYSCALE;
				if (!fmt2jpg(_sourceBuffer, _sourceLen, _sourceWidth, _sourceHeight, fromPixFormat, 12, &_targetBuffer, &_targetLen)) {
					return error.set("fmt2jpg failed");
				}
				break;
			default:
				return error.set(String("cannot convert to JPEG from ") + imageTypeName[_sourceType]);
		}
	}
	log_i("%x Finishing %d %d", this, _targetBuffer != NULL, _targetType);
	if (buffer) std::free(buffer);
	buffer = _targetBuffer;
	len = _targetLen;
	type = _targetType;
	width = _targetWidth;
	height = _targetHeight;
	filename = _sourceFilename;
	timestamp = _targetTimestamp;
	_targetBuffer = NULL;
	return error.clear();
}

// Just load an image from a source without any conversion or scaling
Error& Image::load() {
	// Error checks 
	// Return any existing error first
	log_i("Load %s", _sourceFilename.c_str());
	if (error) return error;

	if (! _from) {
		return error.set("Missing .fromXXX() clause");
	}
	
	if (_sourceFilename == "") {
		log_i("No filename");
		_targetLen = _sourceLen;
		_targetBuffer = (uint8_t*)ps_malloc(_targetLen);
		if (! _targetBuffer) return error.set("Failed to allocate %d PS RAM", _targetLen);
		memcpy(_targetBuffer, _sourceBuffer, _sourceLen);
		_targetWidth = _sourceWidth;
		_targetHeight = _sourceHeight;
		_targetType = _sourceType;
		_targetTimestamp = _sourceTimestamp;
	} else {
		log_i("Filename");
		size_t bytesRead = 0;
		File file;
		if (!_sourceFS->exists(_sourceFilename)) {
			return error.set(String("missing file " + _sourceFilename));
		}
		file = _sourceFS->open(_sourceFilename, FILE_READ);
		
		size_t fileSize = file.size();
		log_i("File size of %s = %d", _sourceFilename, fileSize);
		_targetBuffer = (uint8_t*)ps_malloc(fileSize);
		if (!_targetBuffer) {
			return error.set("Failed to allocate %d PS RAM", fileSize);
		}
		bytesRead = file.readBytes((char*)_targetBuffer, fileSize);
		if (bytesRead != fileSize) {
			std::free(_targetBuffer);
			_targetBuffer = 0;
			return error.set("File read was incomplete");
		}
		_targetLen = fileSize;

		_targetType = _sourceType;
		log_i("Target type %d", _targetType);
		switch(_targetType) {
			case IMAGE_JPEG:
				jpeg.input = _targetBuffer;
				log_i("Starting esp_jpg_decode");
				esp_jpg_decode(_targetLen, JPG_SCALE_NONE, _jpg_read, _jpg_get_size, (void*)&jpeg);
				log_i("Done");
				_targetWidth = jpeg.width;
				_targetHeight = jpeg.height;
				_targetTimestamp.tv_sec = file.getLastWrite();
				_targetTimestamp.tv_usec = 0;
				break;
			case IMAGE_BMP:
				_targetWidth = *(uint16_t*)(_targetBuffer + BMP_WIDTH_ADDR);
				_targetHeight = *(uint16_t*)(_targetBuffer + BMP_HEIGHT_ADDR);
				_targetTimestamp.tv_sec = file.getLastWrite();
				_targetTimestamp.tv_usec = 0;
				break;
			default:
				return error.set("Cannot load %s",imageTypeName[_targetType]);
				break;
		}
	}
	// Replace previous image content if any
	if (buffer) std::free(buffer);
	buffer = _targetBuffer;
	len = _targetLen;
	width = _targetWidth;
	height = _targetHeight;
	type = _targetType;
	timestamp = _targetTimestamp;
	filename = _sourceFilename;
	return error.clear();
}

// Variadic filename formatting
Image& Image::toFile(FS& fs, const char* format, ...) {
	char buffer[FN_BUF_LEN];
	if (! format) {
		error.set("Missing filename format");
		return *this;
	} else {
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
	}
	return toFile(fs, String(buffer));
}

Image& Image::toFile(FS& fs, String filename) {
	File file;
	bool ret = false;
	if (!_targetFilename.startsWith("/")) {
		_targetFilename = String("/") + _targetFilename;
		error.set("Filename should start with /");
		error.soft();
	}
	if (_targetFilename.indexOf("//") >= 0) {
		_targetFilename.replace("//", "/");
		error.set("Filename should not contain //");
		error.soft();
	}

	_targetFilename = filename;
	_targetFS = &fs;
	_to = true;
	
	return *this;
}

Error& Image::save() {
	if (! _to) {
		return error.set("Missing toFile() clause");
	}
	if (_targetFS->exists(_targetFilename) ) {
		log_i("Overwriting existing file %s", filename);
	}
	File file = _targetFS->open(_targetFilename, FILE_WRITE);
	//log_d("Starting to write %s\n", path);
	unsigned long start = millis();
	if (!file) {
		log_e("File open failed %s", filename);
		return error.set("File open failed");
	} else {
		
		if (file.write(buffer, len) != len) {
			log_e("File write fail %s", _targetFilename);
			return error.set("File write failed");
		} else {
		//  log_d("Finished writing to %s\n", path);
		}
		file.close();

		log_i("Wrote file %s in %d ms", _targetFilename, (int)(millis() - start));
		// If save is being done on a background thread the object invoking .save()
		// can check if this->deleteOnSave() is true and then delete the Image (which must have been newed)
		// Can't call delete this as need to return error.clear();
		return error.clear();
	}
}

Error& Image::setPixel(int x, int y, int r, int g, int b) {
	if (type == IMAGE_RGB565) {
		uint16_t pixelVal = r << 11 | g << 5 | b;
		uint8_t h = pixelVal >> 8;
		uint8_t l = pixelVal &0xFF;
		buffer[2 * (y * width + x)] = l;
		buffer[2 * (y * width + x) + 1] = h;
		return error.clear();
	//} else 
	//if (type == IMAGE_RGB888) {
	//
	} else {
		return error.set("Cannot setPixel() for image type %s", imageTypeName[type]);
	}
}

int Image::greyAt(int x, int y) {
	if (x < 0 || x > width || y < 0 || y > height) {
		error.set("Out of bounds (%d, %d)", x, y);
		return -1;
	}
	return pixelAt(x, y).grey();
}

Pixel Image::pixelAt(int x, int y) {
	if (type == IMAGE_RGB565) {
		auto pixBuf = (uint16_t*)buffer;
		return Pixel(pixBuf[y * width + x]);
	} else
	if (type == IMAGE_RGB888) {
		uint8_t* ppixel = buffer + 3 * (y * width + x);
		return Pixel(*(ppixel + 2), *(ppixel + 1), *ppixel); // B G R
	} else 
	if (type == IMAGE_BMP) {
		uint8_t* ppixel = buffer + 3 * (y * width + x) + BMP_HEADER_LEN; // Low endian byte order
		return Pixel(*(ppixel + 2), *(ppixel + 1), *ppixel);
	} else {
		return Pixel(0, 0, 0); // Out of bounds = black
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
		error.set("Images must be the same size for now");
		return -1;
	}
	if (type != IMAGE_RGB565 || that.type != IMAGE_RGB565) {
		error.set("Images should be RGB565 for now");
		return -1;
	}
	if (stride < 1) {
		error.set("Stride must be 1 or more");
		return -1;
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

