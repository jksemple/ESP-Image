# ESP-Image	
 
This library allows images of various sorts to be loaded, converted, compared, edited and saved on ESP32s

## Loading

Images can be loaded from file storage (e.g. SD card), camera (if present), another Image object or a buffer.
The main source types supported are JPEG and BMP.

## Conversion

Images can be converted from their current type to a new type e.g. JPEG to RGB565 or RGB888 to permit editing.

## Editing

Images in either RGB565 or RGB888 can be edited (pixel values altered)

## Comparison

Two images can be compared pixel by pixel using .compareWith(). This takes a comparison function that defines how a pixel pair is to be compared. It must return either true (if there is a difference) or false but the algorithm is up to the developer.  
It also takes an optional masking function which defines whether the pixel pair is to be compared (if true) or skipped (if false).
The compareWith() method itself returns a float which is the ratio of the count of 'different' pixels divided by the count of all pixels that were compared after masking.

## Saving

Images in a saveable format i.e. JPEG or BMP can be saved to storage.  BMP is used to preserve 100% of the detail in the image, JPG is smaller and faster to save but loses some pixel-level detail.

## Metadata

Each Image object has a collection of metadata comprising a label and a string value, which can be used to hold any metadata values that need to accompany images through their app life.  
Metadata is persisted to file storage as a JSON file e.g. foor.jpg has a foo.json metadata file.  There are no default labels or values. None of the metadata values are linked to object properties.
A new entry is created automatically when a label is first used.

In a future release it will be possible to mark some or all of these metadata values to be saved as EXIF properties when saving a JPEG.

## Classes

The main class in this library is Image which is supported with a Pixel class to assist with the RGB565/RGB888 conversions

## Object names

To assist in debugging there is the option to give each Image a 'name' which is used in log_i or log_e messages (if enabled) and exceptions to help localise the source of errors.  Unnamed Images report using their memory address.

## Error handling

Errors arising from logic errors (such as trying to edit a JPG image directly, process an empty image or attempting to malloc too large an object) cause a C++ exception to be tbrown.
This makes error handling easier as the whole of an area of code can be wrapped in a single try/catch block which will catch any such errors in one place instead of needing to check a return value from every method call, which makes code less succinct.
When loading an image from a file the developer has the option whether to either regard a missing file as an exception or to just load an empty image and ignore it.
When saving an image to a file similarly the developer can choose whether the presence of an existing file with the requested name is regarded as an exception or just to overwrite it.

Note that exceptions are costly to process so the developer should aim to structure their code such that exceptions are very rarely or never thrown once code is 'working'.

#### Load and conversion examples
```cpp
Image myImage1, myImage2, myImage3;
myImage1.fromFile(SD, "/abc.jpg").load();
myImage2.fromFile(SD, "/%s/%s.%s", "dirName", "def", "jpg").convertTo(IMAGE_RGB565);
myImage3.fromImage(myImage1).convertTo(IMAGE_RGB888, SCALING_DIVIDE_4);
```
#### Editing example
```cpp
myImage1.setPixel(x, y, 255, 0, 0);
```
#### Comparison example
```cpp
float difference = myImage1.compareWith(myImage2, 1, [], (int x, int y, Pixel thisPixel, Pixel thatPixel) {
	return (thisPixel.grey() != thatPixel.grey());
}, noMask);
```

#### Save example
```cpp
myImage1.toFile(SD, "/abc.jpg").save();
myImage2.toFile(SD, "/%s/%s.%s", "dirName", "def", "bmp").save();

try {
  myImage2.toFile(SD, "/%s/%s.%s", "dirName", "def", "bmp").save(THROW_IF_EXISTS
  );
} catch(std::exception const& ex) {
  Serial.printf("Exception %s\n", ex.what());
}
```

#### Metadata
```cpp
myImage1.metadata["size"] = "640x480";
int brightest = myImage1.metadata["brightest pixel value"].toInt();
```
