# Writer API changelog

## Summary

The iOS package keeps the existing wrapper API and does not add a
ZXingObjC-compatible layer. Barcode generation should be done through
`ZXIBarcodeWriter`.

The old client code used ZXingObjC-style objects:

- `ZXMultiFormatWriter` created a `BitMatrix`.
- `ZXEncodeHints` was passed into `encode(...)`.
- `ZXImage` converted the matrix into `CGImage`.
- `ZXBarcodeFormat` selected the barcode format.

The current wrapper API combines the matrix generation and image conversion in
one call:

- `ZXIBarcodeWriter` writes a barcode directly into `CGImage`.
- `ZXIFormat` selects the barcode format.
- Empty `ZXEncodeHints` are no longer needed.

## Before

```swift
private func generatedCode(string: String, format: ZXBarcodeFormat) -> UIImage? {
    do {
        let writer = ZXMultiFormatWriter()
        let hints = ZXEncodeHints() as ZXEncodeHints
        let result = try writer.encode(string,
                                       format: format,
                                       width: constants.codeSize,
                                       height: constants.codeSize,
                                       hints: hints)

        if let imageRef = ZXImage.init(matrix: result), let image = imageRef.cgimage {
            return UIImage(cgImage: image)
        }
    } catch {
        print(error)
    }
    return nil
}
```

## After

```swift
private func generatedCode(string: String, format: ZXIFormat) -> UIImage? {
    do {
        let writer = ZXIBarcodeWriter()
        let image = try writer.write(
            string,
            width: Int32(constants.codeSize),
            height: Int32(constants.codeSize),
            format: format
        )

        return UIImage(cgImage: image)
    } catch {
        print(error)
        return nil
    }
}
```

Example format values:

```swift
generatedCode(string: value, format: ZXIFormat.QR_CODE)
generatedCode(string: value, format: ZXIFormat.DATA_MATRIX)
generatedCode(string: value, format: ZXIFormat.PDF_417)
generatedCode(string: value, format: ZXIFormat.CODE_128)
```

## Object mapping

| Old object | Current API | Notes |
| --- | --- | --- |
| `ZXMultiFormatWriter` | `ZXIBarcodeWriter` | `ZXIBarcodeWriter.write(...)` uses ZXing's `MultiFormatWriter` internally. |
| `ZXEncodeHints` | Not used for the current scenario | The previous code created empty hints, so removing them does not change behavior. |
| `ZXImage` | Not used | `ZXIBarcodeWriter.write(...)` returns `CGImage` directly. |
| `ZXBarcodeFormat` | `ZXIFormat` | Call sites should pass the current wrapper enum. |

## Notes

- Writer support must be enabled when building `ZXing.xcframework` with
  `-DBUILD_WRITERS=YES`.
- The generated `ZXing.xcframework` must include `MultiFormatWriter.h` and writer
  symbols.
- If client code later starts setting non-empty encode hints, such as margin,
  character encoding, PDF417 compaction, or DataMatrix shape, those options need
  a small targeted extension in the current `ZXIBarcodeWriter` API.
