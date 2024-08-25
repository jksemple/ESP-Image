# ESP-Image
 
This library allows images of various sorts to be loaded, converted, compared, edited and saved on ESP32s

## Loading

Images can be loaded from file storage (e.g. SD card), camera (if present), another Image object or a buffer.
The main source types supported are JPEG and BMP.

## Conversion

Images can be converted from their current type to a new type e.g. JPEG to RGB565 or RGB888 to permit editing.

## Editing

Images in either RGB565 or RGB888 can be edited (pixel values altered)

## Saving

Images in a saveable format i.e. JPEG or BMP can be saved to storage.  BMP is used to preserve 100% of the detail in the image, JPG is smaller and faster to save but loses some pixel-level detail.

## Classes

The main class in this library is Image which is supported with a Pixel class to assist with the RGB565/RGB888 conversions

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
}
```

#### Save example
```cpp
myImage1.toFile(SD, "/abc.jpg").save();
myImage2.toFile(SD, "/%s/%s.%s", "dirName", "def", "bmp").save();
```
