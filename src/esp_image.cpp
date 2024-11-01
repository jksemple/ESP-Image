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

// JPG reader
static unsigned int _jpg_read(void * arg, size_t index, uint8_t *buf, size_t len) {
    jpg_decoder * jpeg = (jpg_decoder *)arg;
    if(buf) {
        memcpy(buf, jpeg->input + index, len);
    }
    return len;
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
                jpeg->output = new uint8_t[(w*h*3)+jpeg->data_offset];
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
	//log_i("In destructor for %s", objectName().c_str());
    if (buffer != nullptr) delete[] buffer;
	//log_i("done");
}
void Image::setObjectName(String name) {
	if (name.length() == 0) {
		char tbuffer[10];
		// Set nickname to be address of object
		sprintf(tbuffer, "%08x", this);
		_objectName = String(tbuffer);
	} else {
		_objectName = name;
	}
}
bool Image::hasContent() { 
	//log_i("%s %s", _objectName, type != IMAGE_NONE && buffer != nullptr ? "has content" : "is empty"); 
    return type != IMAGE_NONE && buffer != nullptr; 
}

String Image::typeName() { return imageTypeName[type]; };
Image& Image::fromBuffer(uint8_t* extBuffer, size_t width, size_t height, size_t extLen, image_type_t imageType, timeval timestamp) {

	if (extBuffer == nullptr || extLen == 0) {
		throw LogicError(StringF("[%s:%d] %s: Buffer is empty", __FILE__, __LINE__, objectName().c_str()));
	}
	//log_i("Buffer width=%d height=%d len=%d type=%d", width, height, len, imageType);

	_sourceBuffer = extBuffer;
	_sourceWidth = width;
	_sourceHeight = height;
	_sourceLen = extLen;
	_sourceType = imageType;
	_sourceTimestamp = timestamp;
	_from = true;

	return *this;
}

Image& Image::fromCamera(camera_fb_t* frame) {

	// NB Don't believe the camera driver when dealing wih custom image sizes
	// Use setTrueSize() to correct the driver width/height
	if (frame->buf == nullptr || frame->len == 0) {
		throw LogicError(StringF("[%s:%d] %s: Nothing captured yet", __FILE__, __LINE__, objectName().c_str()));
	}
	//log_i("%s: setting _sourceBuffer to %x", objectName(), frame->buf);
	_sourceBuffer = frame->buf;
	_sourceLen = frame->len;
	_sourceWidth = frame->width;
	_sourceHeight = frame->height;
	_sourceTimestamp = frame->timestamp;
	_sourceName = "Camera";
	_sourceMetadataPtr = nullptr;
	switch(frame->format) {
		case PIXFORMAT_JPEG:
			_sourceType = IMAGE_JPEG;
			extractJpegSize(_sourceWidth, _sourceHeight, _sourceBuffer, _sourceLen);
			//log_i("Got w = %d, h = %d", _sourceWidth, _sourceHeight);
			break;
		case PIXFORMAT_RGB565:
		case PIXFORMAT_RGB888:
			if (frame->len == frame->width * frame->height * 2) {
				_sourceType = IMAGE_RGB565;
			} else
			if (frame->len == frame->width * frame->height * 3) {		
				_sourceType = IMAGE_RGB888;
			} else {
				throw LogicError(StringF("[%s:%d] %s: camera frame W=%d H=%d Len=%d Format=%s is inconsistent", __FILE__, __LINE__, objectName().c_str(), frame->width, frame->height, frame->len, imageTypeName[frame->format]));
			}
			break;
		case PIXFORMAT_GRAYSCALE:
			if (frame->len == frame->width * frame->height) {
				_sourceType = IMAGE_GRAYSCALE8;
			} else {
				throw LogicError(StringF("[%s:%d] %s: camera frame W=%d H=%d Len=%d Format=%s is inconsistent", __FILE__, __LINE__, objectName().c_str(), frame->width, frame->height, frame->len, imageTypeName[frame->format]));
			}
			break;
		default:
			throw LogicError(StringF("[%s:%d] %s: Format %s is not currently supported", __FILE__, __LINE__, objectName().c_str(), imageTypeName[frame->format]));
	}
	_from = true;

	return *this;
}
uint16_t Image::bigEndianWord(const uint8_t* ptr) {
	uint16_t ret = ptr[0] << 8;
	ret |= ptr[1];
	return ret;
}

uint32_t Image::bigEndianLong(const uint8_t* ptr) {

	log_d("in = %02x %02x %02x %02x", ptr[0], ptr[1], ptr[2], ptr[3]);
	uint32_t ret = ptr[0];
	for(int i = 1; i <= 3; i++) {
		ret << 8;
		ret |= ptr[i];
	}
	log_d("out = %08x", ret);
	return ret;
}

/* 
* Find the next 'segment' in a buffer that may be a whole JPEG or a segment within a JPEG (e.g. an IFD)
* The endPtr parameter is the last byte that may be examined (ie just before the next segment or EOF)
*/
const uint8_t* Image::nextJpegSegment(const uint8_t* startPtr, const uint8_t* endPtr) {
	//log_d("%08x -> %08x", startPtr, endPtr);
	const uint8_t* nextSegmentPtr = startPtr;

	// Check the current segment is valid
	if (*nextSegmentPtr != 0xFF || *(nextSegmentPtr + 1) < 0xC0) {
		//log_d("not FF or <C0 %02x %02x", *nextSegmentPtr, *(nextSegmentPtr + 1));
		return 0;
	}
	jpeg_segment_t* segPtr = (jpeg_segment_t*)nextSegmentPtr;
	if (segPtr->marker == SOI && nextSegmentPtr <= endPtr - 3) {
		nextSegmentPtr += 2;
		segPtr = (jpeg_segment_t*)nextSegmentPtr;
	}
	// Check for end of file
	if (segPtr->marker == EOI) {
		//log_d("EOI");
		return 0;
	}
	// Ensure we can read the next offset
	if (nextSegmentPtr > endPtr - 3) {
		return 0;
	}
	nextSegmentPtr += 2;
	uint32_t offset = bigEndianWord(segPtr->bigendianOffset);
	//log_d("Offset = %d", offset);
	if (nextSegmentPtr + offset > endPtr - 1) {
		//log_d("Out of range after offset");
		return 0;
	}
	nextSegmentPtr += offset;
	//log_d("nextSegmentPtr -> %02x %02x %02x %02x", *nextSegmentPtr, *(nextSegmentPtr+1), *(nextSegmentPtr+2), *(nextSegmentPtr+3) );
	return nextSegmentPtr;
}

void Image::extractJpegSize(uint16_t& width, uint16_t& height, uint8_t* buffer, size_t bufferLen) {
	const uint8_t* endPtr = buffer + bufferLen - 1;
	//log_d("%08x -> %08x", buffer, endPtr);

	for (const uint8_t* segmentPtr = buffer; segmentPtr < endPtr - 1; segmentPtr = nextJpegSegment(segmentPtr, endPtr)) {
		//log_d("SegmentPtr = %08x", segmentPtr);
		if (segmentPtr == 0) {
			//log_d("SegmentPtr == 0");
			break;
		}

		jpeg_segment_t* segPtr = (jpeg_segment_t*)segmentPtr;
		if (segPtr->marker == SOF0) {
			sof0_segment_t* sof0 = (sof0_segment_t*)(segmentPtr + sizeof(jpeg_segment_t));
			width = bigEndianWord(sof0->imageWidth);
			height = bigEndianWord(sof0->imageHeight);
			return;
		} else {
			//log_d("Segment = %02x %02x", segmentPtr[0], segmentPtr[1]);
		}
	}
	//log_d("Exit");
	throw LogicError("SOF0 not found");
}
// Correct the size of a JPEG image by reading the JPEG metadata
// This fixes a problem in the esp-camera driver where custom size images
// requested through sensor->set_res_raw are incorrectly reported in the camera_fb_t buffer
Image& Image::setTrueSize() {
	if (type == IMAGE_JPEG) {

		extractJpegSize(width, height, buffer, len);

		log_i("Corrected (%08x, %d) to %d x %d", &buffer, _sourceLen, width, height);
	}
	return *this;
}

Image& Image::fromImage(Image& sourceImage) {

	if (! sourceImage.hasContent()) {
		throw LogicError(StringF("[%s:%d] %s is empty", __FILE__, __LINE__, sourceImage.objectName().c_str()));
	}
	_sourceBuffer = sourceImage.buffer;
	_sourceLen = sourceImage.len;
	_sourceWidth = sourceImage.width;
	_sourceHeight = sourceImage.height;
	_sourceType = sourceImage.type;
	_sourceTimestamp = sourceImage.timestamp;
	//log_i("sourceImage.objectName() = %s", sourceImage.objectName());
	_sourceName = sourceImage.objectName();
	_sourceMetadataPtr = &sourceImage.metadata;
	_from = true;

	return *this;
}
// Variadic filename formatting
Image& Image::fromFile(FS& fs, const char* format, ...) {
	char fnBuffer[FN_BUF_LEN];
	if (! format) {
		throw LogicError(StringF("[%s:%d] %s: Missing filename format", __FILE__, __LINE__, objectName().c_str()));
	} else {
		va_list args;
		va_start(args, format);
		vsnprintf(fnBuffer, sizeof(fnBuffer), format, args);
	}
	return fromFile(fs, String(fnBuffer));
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
		throw LogicError(StringF("[%s:%d] %s: Cannot infer image type from %s", __FILE__, __LINE__, objectName().c_str(), path.c_str()));
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

	//log_i("%s: Converting from %s to %s", objectName(), source(), imageTypeName[newImageType]);

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
		throw LogicError(StringF("[%s:%d] %s: Source and target types are the same", __FILE__, __LINE__, objectName().c_str()));
	}
	if (_targetType == IMAGE_BMP && _scaling != SCALING_NONE) {
		throw LogicError(StringF("[%s:%d] %s: Cannot scale when converting to BMP", __FILE__, __LINE__, objectName().c_str()));
	}
	if (_targetType == IMAGE_JPEG && _scaling != SCALING_NONE) {
		throw LogicError(StringF("[%s:%d] %s: Cannot scale when converting to JPEG", __FILE__, __LINE__, objectName().c_str()));
	}
	if (_sourceType == IMAGE_JPEG && _targetType == IMAGE_RGB565) {
		//log_i("SourceW = %d, SourceH = %d", _sourceWidth, _sourceHeight);
		_targetWidth = _sourceWidth >> scaling;
		_targetHeight = _sourceHeight >> scaling;
		_targetLen = _targetWidth * _targetHeight * 2;
		//log_i("Heap: %d/%d PSRAM: %d/%d\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize());
		_targetBuffer = new uint8_t[_targetLen];
		//log_i("Heap: %d/%d PSRAM: %d/%d\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize());
		//log_i("JPG2RGB565 %d %d", _sourceLen, _scaling);
		jpeg.input = _sourceBuffer;
		jpeg.output = _targetBuffer;
		esp_jpg_decode(_sourceLen, (jpg_scale_t)_scaling, _jpg_read, _rgb565_write, (void*)&jpeg);
		if (_targetWidth != jpeg.width || _targetHeight != jpeg.height) {
			throw LogicError(StringF("[%s, %d] %s: expected %d x %d but JPEG decoded as %d x %d", __FILE__, __LINE__, objectName().c_str(), _targetWidth, _targetHeight, jpeg.width, jpeg.height));
		}
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
				throw LogicError(StringF("[%s:%d] %s: Cannot convert to BMP from %s", __FILE__, __LINE__, objectName().c_str(), imageTypeName[_sourceType]));
		}

		//log_i("Running fmt2bmp");
		if (!fmt2bmp(_sourceBuffer, _sourceLen, _sourceWidth, _sourceHeight, fromType, &_targetBuffer, &_targetLen)) {
			if (_targetBuffer) delete[] _targetBuffer;
			_targetBuffer = 0;
			throw LogicError(StringF("[%s:%d] fmt2bmp failed", __FILE__, __LINE__));
		} else {
			log_i("%s: to BMP _targetBuffer = %x (%d) %02x %02x", objectName(), _targetBuffer, _targetLen, _targetBuffer[0], _targetBuffer[1]);
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
					if (_targetBuffer) delete[] _targetBuffer;
					_targetBuffer = 0;
					throw LogicError(StringF("[%s:%d] fmt2bmp failed", __FILE__, __LINE__));
				}
				_targetWidth = _sourceWidth;
				_targetHeight = _sourceHeight;
				_targetTimestamp = _sourceTimestamp;
				break;
			case IMAGE_RGB565:
				if (!fmt2jpg(_sourceBuffer, _sourceLen, _sourceWidth, _sourceHeight, PIXFORMAT_RGB565, 12, &_targetBuffer, &_targetLen)) {
					if (_targetBuffer) delete[] _targetBuffer;
					_targetBuffer = 0;
					throw LogicError(StringF("[%s:%d] fmt2bmp failed", __FILE__, __LINE__));
				}
//				log_i("Written to %08x (%d)", _targetBuffer, _targetLen);
				_targetWidth = _sourceWidth;
				_targetHeight = _sourceHeight;
				_targetTimestamp = _sourceTimestamp;
				break;
			case IMAGE_GRAYSCALE8:
				if (!fmt2jpg(_sourceBuffer, _sourceLen, _sourceWidth, _sourceHeight, PIXFORMAT_GRAYSCALE, 12, &_targetBuffer, &_targetLen)) {
					if (_targetBuffer) delete[] _targetBuffer;
					_targetBuffer = 0;
					throw LogicError(StringF("[%s:%d] fmt2bmp failed", __FILE__, __LINE__));
				}
				_targetWidth = _sourceWidth;
				_targetHeight = _sourceHeight;
				_targetTimestamp = _sourceTimestamp;
				break;
			default:
				throw LogicError(StringF("[%s:%d] %s: Cannot convert to JPEG from %s", __FILE__, __LINE__, objectName().c_str(), imageTypeName[_sourceType]));
		}
	}
	//log_i("%s: Buffer is %08x", objectName().c_str(), buffer);
	//log_i("Heap: %d/%d PSRAM: %d/%d", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize());	
	if (buffer != nullptr) delete[] buffer;
	//log_i("Heap: %d/%d PSRAM: %d/%d", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize());	
	//log_i("%s: Setting buffer to %08x", objectName().c_str(), _targetBuffer);
	buffer = _targetBuffer;
	_targetBuffer = 0;
	//log_i("Heap: %d/%d PSRAM: %d/%d", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize());	
	len = _targetLen;
	type = _targetType;
	width = _targetWidth;
	height = _targetHeight;
	timestamp = _targetTimestamp;
	log_i("%s: converted to %s (%d x %d) from %s", objectName().c_str(), typeName(), width, height, source().c_str());
	//log_i("Heap: %d/%d PSRAM: %d/%d", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize());	
	//log_i("Returning");
	return;
}

// Just load an image from a source without any conversion or scaling
void Image::load(missing_image_file_on_load_t missing_file_option) {
	if (! _from) {
		throw LogicError(StringF("[%s:%d] Missing fromXXX() clause", __FILE__, __LINE__));
	}
	//log_i("%s: Load from %s", objectName().c_str(), source().c_str());
	
	std::map<String, String> tempMetadata;

	if (_sourceFilename == "") {
		_targetLen = _sourceLen;
		_targetBuffer = new uint8_t[_targetLen];
		memcpy(_targetBuffer, _sourceBuffer, _sourceLen);
		_targetWidth = _sourceWidth;
		_targetHeight = _sourceHeight;
		_targetType = _sourceType;
		_targetTimestamp = _sourceTimestamp;
		_targetMetadataPtr = _sourceMetadataPtr;
	} else {
		// Reading from FS
		_targetMetadataPtr = &tempMetadata;
		size_t bytesRead = 0;
		File file;
		if (!_sourceFS->exists(_sourceFilename)) {
			if (missing_file_option == IGNORE_MISSING_IMAGE_FILE) {
				log_e("%s: Missing file %s", objectName().c_str(), _sourceFilename.c_str());
				clear();
				return;
			} else {
				throw LogicError(StringF("[%s:%d] Missing file %s", __FILE__, __LINE__, _sourceFilename.c_str()));
			}
		}
		file = _sourceFS->open(_sourceFilename, FILE_READ);
		
		size_t fileSize = file.size();
		//log_i("File size of %s = %d", _sourceFilename, fileSize);
		_targetBuffer = new uint8_t[fileSize];
		bytesRead = file.readBytes((char*)_targetBuffer, fileSize);
		if (bytesRead != fileSize) {
			delete[] _targetBuffer;
			_targetBuffer = 0;
			throw RuntimeError(StringF("[%s:%d] Incomplete file read from %s", __FILE__, __LINE__, _sourceFilename.c_str()));
		}
		_sourceName = _sourceFilename;
		_targetLen = fileSize;

		_targetType = _sourceType;
		//log_i("Target type %d", _targetType);
		switch(_targetType) {
			case IMAGE_JPEG:
				if (! (_targetBuffer[0] == jpg_sig[0] && _targetBuffer[1] == jpg_sig[1])) {
					delete[] _targetBuffer;
					_targetBuffer = 0;
					throw LogicError(StringF("[%s:%d] %s: contents of %s are not %s", __FILE__, __LINE__, objectName().c_str(), _sourceFilename.c_str(), imageTypeName[_targetType]));	
				}
				// A JPG image read from a file contains the width x height in metadata but this must be recovered by decoding
				extractJpegSize(_targetWidth, _targetHeight, _targetBuffer, _targetLen);
				_targetTimestamp.tv_sec = file.getLastWrite();
				_targetTimestamp.tv_usec = 0;
				break;
			case IMAGE_BMP:
				if (! (_targetBuffer[0] == bmp_sig[0] && _targetBuffer[1] == bmp_sig[1])) {
					//log_i("sig[0] = %02x sig[1] = %02x", _targetBuffer[0], _targetBuffer[1]);
					delete[] _targetBuffer;
					_targetBuffer = 0;
					throw LogicError(StringF("[%s:%d] %s: contents of %s are not %s", __FILE__, __LINE__, objectName().c_str(), _sourceFilename.c_str(), imageTypeName[_targetType]));	
				}
				_targetWidth = *(uint32_t*)(_targetBuffer + BMP_WIDTH_ADDR);
				_targetHeight = *(uint32_t*)(_targetBuffer + BMP_HEIGHT_ADDR);
				_targetTimestamp.tv_sec = file.getLastWrite();
				_targetTimestamp.tv_usec = 0;
				break;
			default:
				delete[] _targetBuffer;
				_targetBuffer = 0;
				throw LogicError(StringF("[%s:%d] %s: cannot load %s", __FILE__, __LINE__, objectName().c_str(), imageTypeName[_targetType]));
		}
		// Load any image metadata from FS too
		String metadataFilename = _sourceFilename.substring(0, _sourceFilename.indexOf(".")) + ".json";

		if (_sourceFS->exists(metadataFilename)) {
			tempMetadata.clear();
			auto file = _sourceFS->open(metadataFilename, FILE_READ);
			String startOfFile = readFileToChar(file, '{');
			if (startOfFile.indexOf('{') == -1) 
				throw LogicError(StringF("%s does not contain {", metadataFilename.c_str()));
			while(file.available()) {
				String startOfLine = readFileToChar(file, '{');
				if (startOfLine.indexOf('{') == -1) 
					break;
				String labelValuePair = readFileToChar(file, '}');
				//log_d("labelValuePair = %s", labelValuePair.c_str());
				if (! labelValuePair.endsWith("}"))
					throw LogicError("%s: JSON line should end with }");

				auto fields = split(labelValuePair, '"');
				if (fields.size() != 9)
					throw LogicError(StringF("%s: bad line %s", metadataFilename.c_str(), labelValuePair.c_str()));
				tempMetadata[fields[3]] = fields[7];
			}
			file.close();
		}
		
	} // Finished reading from _sourceFS

	// Replace previous image content if any
	//log_i("%s: Buffer is %08x", objectName(), buffer);
	if (buffer) delete[] buffer;
	//log_i("%s: Setting buffer to %08x", objectName(), _targetBuffer);
	buffer = _targetBuffer;
	_targetBuffer = 0;
	len = _targetLen;
	width = _targetWidth;
	height = _targetHeight;
	type = _targetType;
	timestamp = _targetTimestamp;
	if (_targetMetadataPtr != nullptr) {
		metadata = *_targetMetadataPtr;  // Copy the metadata collection
	}
	if (width == 0 || height == 0) {
		throw LogicError(StringF("%s dimensions were %d x %d", _sourceName.c_str(), width, height));
	}
	log_i("%s: loaded %s (%d x %d) from %s", objectName().c_str(), typeName(), width, height, source().c_str());
 	return;
}

// Variadic filename formatting
Image& Image::toFile(FS& fs, const char* format, ...) {
	char buffer[FN_BUF_LEN];
	if (! format) {
		throw LogicError(StringF("[%s:%d] %s:Missing filename format", __FILE__, __LINE__, objectName().c_str()));
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

void Image::save(existing_image_file_on_save_t existing_file_option) {
	//log_i("In image.save()");
	if (! _to) {
		throw LogicError(StringF("[%s:%d] %s:Missing toFile() clause", __FILE__, __LINE__, objectName().c_str()));
	}
	if (_targetFS->exists(_targetFilename) ) {
		//log_i("Overwriting");
		if (existing_file_option == OVERWRITE_EXISTING_IMAGE_FILE) {
			log_i("%s: Overwriting existing file %s", objectName(), _targetFilename.c_str());
		} else {
			throw LogicError(StringF("[%s:%d] %s:File %s exists", __FILE__, __LINE__, objectName().c_str(), _targetFilename.c_str()));
		}
	} else {
		//log_i("Creating");
		// Check subdirs are in place and create if not
		char* subdirPath = strdup(_targetFilename.c_str());
		//log_i("Subdirpath = %s", subdirPath);
		char* slash = strchr(subdirPath + 1, '/');
		while (slash != nullptr) {
			*slash = '\0';
			if (!_targetFS->exists(subdirPath)) {
				//log_i("mkdir %s", subdirPath);
				if (! _targetFS->mkdir(subdirPath)) {
					throw LogicError(StringF("[%s:%d] %s: failed to mkdir '%s'", __FILE__, __LINE__, objectName().c_str(), subdirPath));
				}
			}
			*slash = '/';
			slash = strchr(slash + 1, '/');
		}
		free(subdirPath);
		//log_i("Finished mkdirs");
	}
	// Write out image
	File file = _targetFS->open(_targetFilename, FILE_WRITE);
	//log_i("Starting to write %s", _targetFilename.c_str());
	unsigned long start = millis();
	if (!file) {
		log_e("File open failed %s", _targetFilename);
		throw LogicError(StringF("[%s:%d] %s:Invalid filename %s", __FILE__, __LINE__, objectName().c_str(), _targetFilename.c_str()));
	} 

	if (file.write(buffer, len) != len) {
		log_e("File write failed %s", _targetFilename);
		throw RuntimeError(StringF("[%s:%d] %s:Incomplete file write to %s", __FILE__, __LINE__, objectName().c_str(), _targetFilename.c_str()));
	} else {
	//  log_d("Finished writing to %s\n", path);
	}
	file.close();
	// Write out metadata if any
	String metadataFilename = _targetFilename.substring(0, _targetFilename.indexOf(".")) + ".json";

	if (metadata.size() > 0) {

		if (_targetFS->exists(metadataFilename) ) {
			//log_i("Overwriting");
			if (existing_file_option == OVERWRITE_EXISTING_IMAGE_FILE) {
				log_i("%s: Overwriting existing file %s", objectName(), metadataFilename.c_str());
			} else {
				throw LogicError(StringF("[%s:%d] %s:File %s exists", __FILE__, __LINE__, objectName().c_str(), metadataFilename.c_str()));
			}
		} else {
			//log_i("Creating");
		}
		// Write out metadata
		File file = _targetFS->open(metadataFilename, FILE_WRITE);

		unsigned long start = millis();
		if (!file) {
			log_e("File open failed %s", metadataFilename.c_str());
			throw LogicError(StringF("[%s:%d] %s:Invalid filename %s", __FILE__, __LINE__, objectName().c_str(), metadataFilename.c_str()));
		} 
		String json = "{ \"metadata\" : [";
		for (auto el : metadata) {
			String line = StringF("{ \"%s\": \"%s\", \"%s\": \"%s\" }\n", "label", el.first, "value", el.second);
			//log_i("Line %s", line.c_str());
			json += line;
		}
		json += "]\n}";

		if (file.write((const uint8_t*)json.c_str(), json.length()) != json.length()) {
			log_e("File write failed %s", metadataFilename);
			throw RuntimeError(StringF("[%s:%d] %s:Incomplete file write to %s", __FILE__, __LINE__, objectName().c_str(), metadataFilename.c_str()));
		} else {
		//  log_d("Finished writing to %s\n", path);
		}
		file.close();

	} else {
		// No metadata so remove what might be there
		if (_targetFS->exists(metadataFilename) ) {
			//log_i("Deleting metadata");
			_targetFS->remove(metadataFilename);
		}

	}
	//log_i("%s: Wrote file %s in %d ms", objectName().c_str(), _targetFilename.c_str(), (int)(millis() - start));
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
		throw LogicError(StringF("[%s:%d] %s: Cannot setPixel for %s", __FILE__, __LINE__, objectName().c_str(), typeName().c_str()));
	}
}

int Image::greyAt(int x, int y) {
	if (x < 0 || x > width || y < 0 || y > height) {
		throw LogicError(StringF("[%s:%d] %s: %d, %d is out of bounds", __FILE__, __LINE__, objectName().c_str(), x, y));
	}
	return pixelAt(x, y).grey();
}

Pixel Image::pixelAt(int x, int y) {
	if (x < 0 || x >= width || y < 0 || y >= height) {
		throw LogicError(StringF("[%s:%d] %s:%d, %d is out of bounds", __FILE__, __LINE__, objectName().c_str(), x, y));
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
		throw LogicError(StringF("[%s:%d] %s: Cannot get pixelAt() for %s", __FILE__, __LINE__, objectName().c_str(), typeName().c_str()));
	}
}
bool outsideCircle (int x, int y, int width, int height) {
    return ((x - width / 2 )*(x - width / 2) + (y - height / 2)*(y - height / 2) 
     >= width * width / 4);
};

bool insideCircle(int x , int y, int width, int height) {
	return ((x - width / 2)*(x - width / 2) + (y - height / 2)*(y - height / 2) 
      < width * width / 4);
}

bool insideCentralCircle(int x , int y, int width, int height) {
	return ((x - width / 2)*(x - width / 2) + (y - height / 2)*(y - height / 2) 
      < width * width / 16);
}
bool noMask(int x, int y, int width, int height) {
	return true;
}

// Compare this image with another similar one
// The comparisonFunction can be a lambda or some other form of std::function but it must:
// - accept an x and y position of Pixels being compared
// - accept the Pixel values at those coords from both images
// - return a true/false value that indicates if the compared Pixels differ in some arbitrary way
//   by more than a threshold value.  False indicates no significant difference
// The compareWith() method returns a float being the number of such differing pixels divided by the number of pixels checked.
float Image::compareWith(Image& that, int stride, comparisonFunction compareFunc, maskFunction maskFunc) {
	if (width != that.width && height != that.height) {
		throw LogicError(StringF("[%s:%d] %s and %s are not the same size", __FILE__, __LINE__, objectName().c_str(), that.objectName().c_str()));
	}
	if (type != IMAGE_RGB565 || that.type != IMAGE_RGB565) {
		throw LogicError(StringF("[%s:%d] %s and %s are not the same type", __FILE__, __LINE__, objectName().c_str(), that.objectName().c_str()));
	}
	if (stride < 1) {
		throw LogicError(StringF("[%s:%d] %s: Stride must be 1 or more", __FILE__, __LINE__, objectName().c_str()));
	}
	int comparedCount = 0;
	int diffCount = 0;
	for (int y = 0; y < height; y += stride) {
		for (int x = 0; x < width; x += stride) {
			if (maskFunc == nullptr || maskFunc(x, y, width, height)) {
				comparedCount ++;
				diffCount += compareFunc(x, y, pixelAt(x, y), that.pixelAt(x, y)) ? 1 : 0;
			}
		}
	}
	log_i("diffCount = %d, compared = %d", diffCount, comparedCount);
	return (float)diffCount / comparedCount;
}

int Image::maxGrey(maskFunction maskFunc) {
	if (type != IMAGE_RGB565) {
		throw LogicError(StringF("[%s:%d] %s should be RGB565", __FILE__, __LINE__, objectName().c_str(), objectName()));
	}
	int maxGrey = 0;
	for (int y = 0; y < height; y += 1) {
		for (int x = 0; x < width; x += 1) {
			if (maskFunc == nullptr || maskFunc(x, y, width, height)) {
				if (greyAt(x, y) > maxGrey) maxGrey = greyAt(x, y);
			}
		}
	}
	return maxGrey;
}

int Image::minGrey(maskFunction maskFunc) {
	if (type != IMAGE_RGB565) {
		throw LogicError(StringF("[%s:%d] %s should be RGB565", __FILE__, __LINE__, objectName().c_str(), objectName()));
	}
	int minGrey = 0;
	for (int y = 0; y < height; y += 1) {
		for (int x = 0; x < width; x += 1) {
			if (maskFunc == nullptr || maskFunc(x, y, width, height)) {
				if (greyAt(x, y) < minGrey) minGrey = greyAt(x, y);
			}
		}
	}
	return minGrey;
}
void Image::foreachPixel(maskFunction maskFunc, actionFunction actionFunc) {
	if (type != IMAGE_RGB565) {
		throw LogicError(StringF("[%s:%d] %s should be RGB565", __FILE__, __LINE__, objectName().c_str(), objectName()));
	}
	for (int y = 0; y < height; y += 1) {
		for (int x = 0; x < width; x += 1) {
			if (maskFunc == nullptr || maskFunc(x, y, width, height)) {
				actionFunc(x, y, pixelAt(x, y));
			}
		}
	}
}

void Image::clear() {
	if (buffer) {
		delete[] buffer;
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

String Image::readFileToChar(File& file, char endChar) {
	String ret = "";

	while(file.available()) {
		char c = file.read();
		ret += c;
		if (c == endChar) {
			return ret;
		}
	}
	return ret;
}

std::vector<String> Image::split(const String& source, char splitChar) {

	std::vector<String> strings;
	size_t current, previous = 0;
	current = source.indexOf(splitChar);
	String section;
	while (current != -1) {
		section = source.substring(previous, current);
		section.trim();
		strings.push_back(section);
		previous = current + 1;
		current = source.indexOf(splitChar, previous);
	}
	section = source.substring(previous);
	section.trim();
	strings.push_back(section);
	return strings;
}