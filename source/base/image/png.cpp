//******************************************************************************
///
/// @file base/image/png.cpp
///
/// Implementation of Portable Network Graphics (PNG) image file handling.
///
/// @note
///     Some functions implemented in this file throw exceptions from 'extern C'
///     code. be sure to enable this in your compiler options (preferably for
///     this file only).
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.7.
/// Copyright 1991-2016 Persistence of Vision Raytracer Pty. Ltd.
///
/// POV-Ray is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License as
/// published by the Free Software Foundation, either version 3 of the
/// License, or (at your option) any later version.
///
/// POV-Ray is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU Affero General Public License for more details.
///
/// You should have received a copy of the GNU Affero General Public License
/// along with this program.  If not, see <http://www.gnu.org/licenses/>.
///
/// ----------------------------------------------------------------------------
///
/// POV-Ray is based on the popular DKB raytracer version 2.12.
/// DKBTrace was originally written by David K. Buck.
/// DKBTrace Ver 2.0-2.12 were written by David K. Buck & Aaron A. Collins.
///
/// @endparblock
///
//******************************************************************************

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "base/image/png_pov.h"

#ifndef LIBPNG_MISSING

#include <string>

// Boost header files
#include <boost/scoped_ptr.hpp>
#include <boost/scoped_array.hpp>

#include <png.h>

// POV-Ray base header files
#include "base/fileinputoutput.h"
#include "base/types.h"
#include "base/image/metadata.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov_base
{

namespace Png
{

/*****************************************************************************
* Local preprocessor defines
******************************************************************************/

/* Number of scanlines between output flushes, and hence the maximum number of
 * lines lost for an interrupted render.  Note that making it much smaller
 * than about 10 for a 640x480 image will noticably degrade compression.
 * If a very small buffer is specified, we don't want to flush more than once
 * every 10 lines or so (assuming a 2:1 compression ratio).
 */
const int FLUSH_DIST = 16; // Set to 16 and removed complicated math [trf]
const int NTEXT = 15;      // Maximum number of tEXt comment blocks
const int MAXTEXT = 1024;  // Maximum length of a tEXt message


/*****************************************************************************
* Local typedefs
******************************************************************************/
struct Messages
{
    vector<string> warnings;
    string error;
};

/*****************************************************************************
* Local variables
******************************************************************************/

/*****************************************************************************
* Static functions
******************************************************************************/


extern "C"
{
    // These are replacement error and warning functions for the libpng code
    void png_pov_err(png_structp, png_const_charp);
    void png_pov_warn(png_structp, png_const_charp);
    void png_pov_read_data(png_structp, png_bytep, png_size_t);
    void png_pov_write_data(png_structp, png_bytep, png_size_t);
    void png_pov_flush_data(png_structp);


    /*****************************************************************************
    *
    * FUNCTION      : png_pov_warn
    *
    * ARGUMENTS     : png_struct *png_ptr; char *msg;
    *
    * MODIFIED ARGS :
    *
    * RETURN VALUE  :
    *
    * AUTHOR        : Andreas Dilger
    *
    * DESCRIPTION
    *
    *   Prints an warning message using the POV I/O functions.  This uses the
    *   png io_ptr to determine whether error messages should be printed or
    *   not.
    *
    * CHANGES
    *
    ******************************************************************************/

    void png_pov_warn(png_structp png_ptr, png_const_charp msg)
    {
        Messages *m = reinterpret_cast<Messages *>(png_get_error_ptr(png_ptr));

        if (m)
            m->warnings.push_back (string(msg)) ;
    }


    /*****************************************************************************
    *
    * FUNCTION      : png_pov_err
    *
    * ARGUMENTS     : png_struct *png_ptr; char *msg;
    *
    * MODIFIED ARGS :
    *
    * RETURN VALUE  :
    *
    * AUTHOR        : Andreas Dilger
    *
    * DESCRIPTION
    *
    *   If the png io_ptr is true, this prints an error message using the POV
    *   I/O function, then throws an exception.
    *
    * CHANGES
    *
    ******************************************************************************/

    void png_pov_err(png_structp png_ptr, png_const_charp msg)
    {
        Messages *m = reinterpret_cast<Messages *>(png_get_error_ptr(png_ptr));

        if (m)
            m->error = string(msg) ;
        throw POV_EXCEPTION(kFileDataErr, msg);
    }


    /*****************************************************************************
    *
    * FUNCTION      : png_pov_read_data
    *
    * ARGUMENTS     : png_structp png_ptr; png_bytep data; png_uint_32 length;
    *
    * MODIFIED ARGS :
    *
    * RETURN VALUE  :
    *
    * AUTHOR        : Thorsten Froehlich
    *
    * DESCRIPTION
    *
    *   Replacement read function.
    *
    * CHANGES
    *
    ******************************************************************************/

    void png_pov_read_data(png_structp png_ptr, png_bytep data, png_size_t length)
    {
        IStream *file = reinterpret_cast<IStream *>(png_get_io_ptr(png_ptr));

        if (!file->read (data, length))
        {
            Messages *m = reinterpret_cast<Messages *>(png_get_error_ptr(png_ptr));
            if (m)
                m->error = string("Cannot read PNG data");
            throw POV_EXCEPTION(kFileDataErr, "Cannot read PNG data");
        }
    }


    /*****************************************************************************
    *
    * FUNCTION      : png_pov_write_data
    *
    * ARGUMENTS     : png_structp png_ptr; png_bytep data; png_uint_32 length;
    *
    * MODIFIED ARGS :
    *
    * RETURN VALUE  :
    *
    * AUTHOR        : Thorsten Froehlich
    *
    * DESCRIPTION
    *
    *   Replacement write function.
    *
    * CHANGES
    *
    ******************************************************************************/

    void png_pov_write_data(png_structp png_ptr, png_bytep data, png_size_t length)
    {
        OStream *file = reinterpret_cast<OStream *>(png_get_io_ptr(png_ptr));

        if (!file->write (data, length))
        {
            Messages *m = reinterpret_cast<Messages *>(png_get_error_ptr(png_ptr));
            if (m)
                m->error = string("Cannot write PNG data");
            throw POV_EXCEPTION(kFileDataErr, "Cannot write PNG data");
        }
    }


    /*****************************************************************************
    *
    * FUNCTION      : png_pov_flush_data
    *
    * ARGUMENTS     : png_structp png_ptr;
    *
    * MODIFIED ARGS :
    *
    * RETURN VALUE  :
    *
    * AUTHOR        : Thorsten Froehlich
    *
    * DESCRIPTION
    *
    *   Replacement flush function.
    *
    * CHANGES
    *
    ******************************************************************************/

    void png_pov_flush_data(png_structp png_ptr)
    {
        OStream *file = reinterpret_cast<OStream *>(png_get_io_ptr(png_ptr));
        file->flush();
    }

    static bool ReadPNGUpdateInfo (png_structp png_ptr, png_infop info_ptr)
    {
        if (setjmp(png_jmpbuf(png_ptr)))
        {
            // If we get here, we had a problem reading the file
            return (false) ;
        }
        png_read_update_info(png_ptr, info_ptr);
        return (true);
    }

    static bool ReadPNGImage (png_structp r_png_ptr, png_bytepp row_ptrs)
    {
        if (setjmp(png_jmpbuf(r_png_ptr)))
        {
            // If we get here, we had a problem reading the file
            return (false) ;
        }
        png_read_image(r_png_ptr, row_ptrs);
        return (true);
    }
}

/*****************************************************************************
*
* FUNCTION      : Read_Png_Image
*
* ARGUMENTS     : IMAGE *Image; char *name;
*
* MODIFIED ARGS : Image
*
* RETURN VALUE  : none
*
* AUTHOR        : Andreas Dilger
*
* DESCRIPTION
*
*   Reads a PNG image into an RGB image buffer
*
* CHANGES
*
*   Updated for POV-Ray 3.X - [TIW]
*   Updated to allow grayscale and alpha together, Oct 1995 - [AED]
*   Fixed palette size for grayscale images with bit-depth <= 8, Nov 1995 [AED]
*   Changed how grayscale images > 8bpp are stored based on use, Nov 1995 [AED]
*
******************************************************************************/

// TODO: use scoped clean-up class in case of exception
Image *Read (IStream *file, const Image::ReadOptions& options)
{
    int                   stride;
    int                   j;
    png_info              *r_info_ptr;
    png_byte              **row_ptrs;
    png_struct            *r_png_ptr;
    unsigned int          width;
    unsigned int          height;
    Messages              messages;
    Image                 *image = NULL;

    if ((r_png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)(&messages), png_pov_err, png_pov_warn)) == NULL)
        throw POV_EXCEPTION(kOutOfMemoryErr, "Cannot allocate PNG data structures");
    if ((r_info_ptr = png_create_info_struct(r_png_ptr)) == NULL)
        throw POV_EXCEPTION(kOutOfMemoryErr, "Cannot allocate PNG data structures");

    // set up the input control
    png_set_read_fn(r_png_ptr, file, png_pov_read_data);

    // read the file information
    png_read_info(r_png_ptr, r_info_ptr);

    // PNG files are pretty clear about Gamma:
    // - If an ICC profile is present, that information should be used (but currently we don't, because that would be a lot of work).
    // - Otherwise, if an sRGB chunk is present, the image uses sRGB color space.
    // - Otherwise, if a gAMA chunk is present, the image uses the specified power-law gamma encoding.
    // - Otherwise, PNG/W3C recommendation is to assume that the image uses sRGB color space nonetheless.
    GammaCurvePtr gamma;
    if (options.gammacorrect)
    {
        if (options.gammaOverride) // user wants to override information, let them have their will
            gamma = TranscodingGammaCurve::Get(options.workingGamma, options.defaultGamma);
#if defined(PNG_sRGB_SUPPORTED) && defined(PNG_READ_sRGB_SUPPORTED)
        else if (png_get_valid(r_png_ptr, r_info_ptr, PNG_INFO_sRGB)) // file has a sRGB chunk, indicating sRGB color space
        {
            gamma = TranscodingGammaCurve::Get(options.workingGamma, SRGBGammaCurve::Get());
        }
#endif
#if defined(PNG_gAMA_SUPPORTED) && defined(PNG_READ_gAMA_SUPPORTED)
        else if (png_get_valid(r_png_ptr, r_info_ptr, PNG_INFO_gAMA)) // file has a gAMA chunk, indicating the encoding gamma
        {
            double file_gamma = 1/2.2;
            png_get_gAMA(r_png_ptr, r_info_ptr, &file_gamma);
            gamma = TranscodingGammaCurve::Get(options.workingGamma, PowerLawGammaCurve::GetByEncodingGamma(file_gamma));
        }
#endif
        else
        {
            // no gamma info; PNG/W3C recommends to expect sRGB
            if (options.defaultGamma)
                gamma = TranscodingGammaCurve::Get(options.workingGamma, options.defaultGamma);
            else
                gamma = TranscodingGammaCurve::Get(options.workingGamma, SRGBGammaCurve::Get());
        }
    }

    // PNG is specified to use non-premultiplied alpha, so that's the preferred mode to use for the image container unless the user overrides
    // (e.g. to handle a non-compliant file).
    bool premul = false;
    if (options.premultipliedOverride)
        premul = options.premultiplied;

    width = png_get_image_width(r_png_ptr, r_info_ptr);
    height = png_get_image_height(r_png_ptr, r_info_ptr);

    stride = 1;

    png_byte color_type = png_get_color_type(r_png_ptr, r_info_ptr);
    png_byte bit_depth = png_get_bit_depth(r_png_ptr, r_info_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
    {
        // color palette image
        vector<Image::RGBAMapEntry> colormap ;
        Image::RGBAMapEntry entry;
        png_colorp palette;
        int cmap_len;
        png_get_PLTE(r_png_ptr, r_info_ptr, &palette, &cmap_len);
        bool has_alpha = png_get_valid(r_png_ptr, r_info_ptr, PNG_INFO_tRNS);
        png_bytep trans;
        int num_trans;
        png_get_tRNS(r_png_ptr, r_info_ptr, &trans, &num_trans, NULL);

        for(int index = 0; index < cmap_len; index++)
        {
            entry.red   = IntDecode(gamma, palette[index].red,   255);
            entry.green = IntDecode(gamma, palette[index].green, 255);
            entry.blue  = IntDecode(gamma, palette[index].blue,  255);
            entry.alpha = 1.0f ;
            if (has_alpha)
                if (index < num_trans)
                    entry.alpha = IntDecode(trans[index], 255);
            colormap.push_back (entry);
        }

        Image::ImageDataType imagetype = options.itype;
        if (imagetype == Image::Undefined)
            imagetype = Image::Colour_Map;
        image = Image::Create (width, height, imagetype, colormap) ;
        image->SetPremultiplied(premul); // specify whether the color map data has premultiplied alpha
        gamma.reset(); // gamma has been taken care of by transforming the color table.

        // tell pnglib to expand data to 1 pixel/byte
        png_set_packing(r_png_ptr);
    }
    else if ((color_type == PNG_COLOR_TYPE_GRAY) && (bit_depth <= 8))
    {
        // grayscale image
        // (To keep the actual data reading code simple, we're pretending this to be a color palette image.)
        vector<Image::RGBAMapEntry> colormap ;
        Image::RGBAMapEntry entry;
        int cmap_len = 1 << bit_depth;
        bool has_alpha = png_get_valid(r_png_ptr, r_info_ptr, PNG_INFO_tRNS);
        png_bytep trans;
        int num_trans;
        png_get_tRNS(r_png_ptr, r_info_ptr, &trans, &num_trans, NULL);

        for(int index = 0; index < cmap_len; index++)
        {
            entry.red = entry.green = entry.blue = IntDecode(gamma, index, cmap_len - 1);
            entry.alpha = 1.0f ;
            if (has_alpha)
                if (index < num_trans)
                    entry.alpha = IntDecode(trans[index], 255);
            colormap.push_back (entry);
        }

        Image::ImageDataType imagetype = options.itype;
        if (imagetype == Image::Undefined)
            imagetype = has_alpha ? Image::GrayA_Int8 : Image::Gray_Int8;
        image = Image::Create (width, height, imagetype, colormap) ;
        image->SetPremultiplied(premul); // specify whether the color map data has premultiplied alpha
        gamma.reset(); // gamma has been taken care of by transforming the color table.

        // tellg pnglib to expand data to 1 pixel/byte
        png_set_packing(r_png_ptr);
    }
    else if ((color_type & PNG_COLOR_MASK_COLOR) == PNG_COLOR_TYPE_GRAY)
    {
        // grayscale image
        stride = bit_depth <= 8 ? 1 : 2 ;
        if (color_type & PNG_COLOR_MASK_ALPHA)
            stride *= 2 ;
    }
    else if ((color_type == PNG_COLOR_TYPE_RGB) || (color_type == PNG_COLOR_TYPE_RGB_ALPHA))
    {
        // color image
        stride = color_type == PNG_COLOR_TYPE_RGB ? 3 : 4 ;
        if (bit_depth > 8)
            stride *= 2 ;
    }
    else
    {
        png_destroy_read_struct(&r_png_ptr, &r_info_ptr, (png_infopp)NULL);
        throw POV_EXCEPTION(kFileDataErr, "Unsupported color type in PNG image") ;
    }

    png_set_interlace_handling(r_png_ptr);

    if (ReadPNGUpdateInfo(r_png_ptr, r_info_ptr) == false)
    {
        png_destroy_read_struct(&r_png_ptr, &r_info_ptr, (png_infopp)NULL);
        if (messages.error.length() > 0)
            throw POV_EXCEPTION(kFileDataErr, messages.error.c_str());
        throw POV_EXCEPTION(kFileDataErr, "Cannot read PNG image.");
    }

    // Allocate row buffers for the input
    row_ptrs = new png_bytep [height];

    for(int row = 0; row < height; row++)
        row_ptrs[row] = new png_byte [png_get_rowbytes(r_png_ptr, r_info_ptr)];

    // Read in the entire image. we call a 'C' function for this as the PNG code
    // calls longjmp in the case of errors, and we'd rather keep that out of here.
    // NOTE: the above comment is probably out of date as of 20070325, but the
    // below code is still valid so there's no need to change it.
    if (ReadPNGImage (r_png_ptr, row_ptrs) == false)
    {
        for(int row = 0; row < height; row++)
            delete[] row_ptrs [row] ;
        delete[] row_ptrs;
        png_destroy_read_struct(&r_png_ptr, &r_info_ptr, (png_infopp)NULL);
        if (messages.error.length() > 0)
            throw POV_EXCEPTION(kFileDataErr, messages.error.c_str());
        throw POV_EXCEPTION(kFileDataErr, "Cannot read PNG image.");
    }

    // We must copy all the values because PNG supplies RGBRGB, but POV-Ray
    // stores RGB components in separate arrays
    if (image == NULL) // image will only be NULL at this point if the file is not a color-mapped type
    {
        bool has_alpha = (color_type & PNG_COLOR_MASK_ALPHA) != 0 ;

        if ((color_type & PNG_COLOR_MASK_COLOR) == PNG_COLOR_TYPE_GRAY)
        {
            // grayscale image
            // (note that although 8-bit greyscale images should have been handled earlier already, 8-bit greyscale + 8-bit alpha images haven't.)

            Image::ImageDataType imagetype = options.itype;

            if (imagetype == Image::Undefined)
            {
                if (GammaCurve::IsNeutral(gamma))
                {
                    if (has_alpha)
                        imagetype = bit_depth > 8 ? Image::GrayA_Int16 : Image::GrayA_Int8;
                    else
                        imagetype = bit_depth > 8 ? Image::Gray_Int16 : Image::Gray_Int8;
                }
                else
                {
                    if (has_alpha)
                        imagetype = bit_depth > 8 ? Image::GrayA_Gamma16 : Image::GrayA_Gamma8;
                    else
                        imagetype = bit_depth > 8 ? Image::Gray_Gamma16 : Image::Gray_Gamma8;
                }
            }

            image = Image::Create (width, height, imagetype);
            image->SetPremultiplied(premul); // set desired storage mode regarding alpha premultiplication
            if (bit_depth <= 8)
                image->TryDeferDecoding(gamma, 255); // try to have gamma adjustment being deferred until image evaluation.
            else
                image->TryDeferDecoding(gamma, 65535); // try to have gamma adjustment being deferred until image evaluation.

            if (has_alpha)
            {
                // with alpha channel
                if (bit_depth <= 8)
                {
                    for(int row = 0; row < height; row++)
                        for(int col = 0, j = 0; col < width; col++, j += stride)
                            SetEncodedGrayAValue (image, col, row, gamma, 255, (unsigned int) row_ptrs[row][j], row_ptrs[row][j + 1], premul) ;
                }
                else
                {
                    for(int row = 0; row < height; row++)
                    {
                        for(int col = j = 0; col < width; col++, j += stride)
                        {
                            unsigned int c = ((unsigned short)row_ptrs[row][j] << 8) | row_ptrs[row][j + 1];
                            unsigned int a = (((unsigned short)row_ptrs[row][j + 2] << 8) | row_ptrs[row][j + 3]);
                            SetEncodedGrayAValue (image, col, row, gamma, 65535, c, a, premul) ;
                        }
                    }
                }
            }
            else
            {
                // without alpha channel
                if (bit_depth <= 8)
                {
                    for(int row = 0; row < height; row++)
                        for(int col = j = 0; col < width; col++, j += stride)
                            SetEncodedGrayValue (image, col, row, gamma, 255, (unsigned int) row_ptrs[row][j]) ;
                }
                else
                {
                    for(int row = 0; row < height; row++)
                        for(int col = j = 0; col < width; col ++, j += stride)
                            SetEncodedGrayValue (image, col, row, gamma, 65535, ((unsigned int)row_ptrs[row][j] << 8) | row_ptrs[row][j + 1]) ;
                }
            }
        }
        else
        {
            // color image ((color_type & PNG_COLOR_MASK_COLOR) != 0)
            Image::ImageDataType imagetype = options.itype;

            if (imagetype == Image::Undefined)
            {
                if (GammaCurve::IsNeutral(gamma))
                {
                    if (has_alpha)
                        imagetype = bit_depth > 8 ? Image::RGBA_Int16 : Image::RGBA_Int8;
                    else
                        imagetype = bit_depth > 8 ? Image::RGB_Int16 : Image::RGB_Int8;
                }
                else
                {
                    if (has_alpha)
                        imagetype = bit_depth > 8 ? Image::RGBA_Gamma16 : Image::RGBA_Gamma8;
                    else
                        imagetype = bit_depth > 8 ? Image::RGB_Gamma16 : Image::RGB_Gamma8;
                }
            }

            image = Image::Create (width, height, imagetype);
            image->SetPremultiplied(premul); // set desired storage mode regarding alpha premultiplication
            if (bit_depth <= 8)
                image->TryDeferDecoding(gamma, 255); // try to have gamma adjustment being deferred until image evaluation.
            else
                image->TryDeferDecoding(gamma, 65535); // try to have gamma adjustment being deferred until image evaluation.

            if (bit_depth <= 8)
            {
                for(int row = 0; row < height; row++)
                {
                    for(int col = j = 0; col < width; col++, j += stride)
                    {
                        unsigned int r = row_ptrs[row][j];
                        unsigned int g = row_ptrs[row][j + 1];
                        unsigned int b = row_ptrs[row][j + 2];
                        if (has_alpha)
                            SetEncodedRGBAValue (image, col, row, gamma, 255, r, g, b, row_ptrs[row][j + 3], premul);
                        else
                            SetEncodedRGBValue (image, col, row, gamma, 255, r, g, b);
                    }
                }
            }
            else
            {
                for(int row = 0; row < height; row++)
                {
                    for(int col = j = 0; col < width; col++, j += stride)
                    {
                        unsigned int r = ((unsigned int)row_ptrs[row][j] << 8) | row_ptrs[row][j + 1];
                        unsigned int g = ((unsigned int)row_ptrs[row][j + 2] << 8) | row_ptrs[row][j + 3];
                        unsigned int b = ((unsigned int)row_ptrs[row][j + 4] << 8) | row_ptrs[row][j + 5];
                        if (has_alpha)
                            SetEncodedRGBAValue (image, col, row, gamma, 65535, r, g, b, (((unsigned int)row_ptrs[row][j + 6] << 8) | row_ptrs[row][j + 7]), premul) ;
                        else
                            SetEncodedRGBValue (image, col, row, gamma, 65535, r, g, b) ;
                    }
                }
            }
        }
    }
    else
    {
        // grayscale palette image or color palette image
        for (int row = 0; row < height; row++)
            for (int col = j = 0; col < width; col++, j += stride)
                image->SetIndexedValue (col, row, row_ptrs [row] [j]) ;
    }

    // Clean up the rest of the PNG memory and such
    for(int row = 0; row < height; row++)
        delete[] row_ptrs [row] ;
    delete[] row_ptrs;

    // clean up after the read, and free any memory allocated
    png_destroy_read_struct(&r_png_ptr, &r_info_ptr, (png_infopp)NULL);

    if (messages.error.length() > 0)
        throw POV_EXCEPTION(kFileDataErr, messages.error.c_str());

    options.warnings = messages.warnings ;
    return (image) ;
}

void Write (OStream *file, const Image *image, const Image::WriteOptions& options)
{
    int             himask;
    int             png_stride;
    int             width = image->GetWidth() ;
    int             height = image->GetHeight() ;
    int             j;
    int             bpcc = options.bpcc;
    bool            use_alpha = image->HasTransparency() && options.alphachannel;
    unsigned int    color;
    unsigned int    alpha;
    unsigned int    r;
    unsigned int    g;
    unsigned int    b;
    unsigned int    maxValue;
    unsigned int    hiShift;
    unsigned int    loShift;
    png_info        *info_ptr = NULL;
    png_struct      *png_ptr  = NULL;
    Messages        messages;
    GammaCurvePtr   gamma;
    Metadata        meta;
    DitherHandler*  dither = options.dither.get();

    if (options.encodingGamma)
        gamma = TranscodingGammaCurve::Get(options.workingGamma, options.encodingGamma);
    else
        // PNG/W3C recommends to use sRGB color space
        gamma = TranscodingGammaCurve::Get(options.workingGamma, SRGBGammaCurve::Get());

    // PNG is specified to use non-premultiplied alpha, so that's the way we do it unless the user overrides
    // (e.g. to handle a non-compliant file).
    bool premul = false;
    if (options.premultiplyOverride)
        premul = options.premultiply;

    if (bpcc == 0)
        bpcc = image->GetMaxIntValue() == 65535 ? 16 : 8 ;
    else if (bpcc < 5)
        bpcc = 5 ;
    else if (bpcc > 16)
        bpcc = 16 ;

    // special case: if options.grayscale is set, we enforce 16bpp irregardless of other settings
    if (options.grayscale)
        bpcc = 16;

    maxValue = (1<<bpcc)-1;

    if ((png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)(&messages), png_pov_err, png_pov_warn)) == NULL)
        throw POV_EXCEPTION(kOutOfMemoryErr, "Cannot allocate PNG data structures");
    if ((info_ptr = png_create_info_struct(png_ptr)) == NULL)
        throw POV_EXCEPTION(kOutOfMemoryErr, "Cannot allocate PNG data structures");

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        // If we get here, we had a problem writing the file
        png_destroy_write_struct(&png_ptr, &info_ptr);
        throw POV_EXCEPTION(kFileDataErr, "Error writing PNG file") ;
    }

    // Set up the compression structure
    png_set_write_fn (png_ptr, file, png_pov_write_data, png_pov_flush_data);

    bool use_color = !(image->IsGrayscale() | options.grayscale);

    // Fill in the relevant image information
    png_set_IHDR(png_ptr, info_ptr,
                 width, height,
                 8 * ((bpcc + 7) / 8), // bit_depth
                 (use_color ? PNG_COLOR_MASK_COLOR : 0) | (use_alpha ? PNG_COLOR_MASK_ALPHA : 0), // color_type
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT // interlace_method
                 );

#if defined(PNG_WRITE_sBIT_SUPPORTED)
    png_color_8 sbit = {0,0,0,0,0};
    if (use_color)
        sbit.red = sbit.green = sbit.blue = bpcc;
    else
        sbit.gray = bpcc;
    if (use_alpha)
        sbit.alpha = bpcc;
    png_set_sBIT(png_ptr, info_ptr, &sbit);
#endif // PNG_WRITE_sBIT_SUPPORTED

#if defined(PNG_WRITE_gAMA_SUPPORTED)
    png_set_gAMA(png_ptr, info_ptr, 1.0f / (options.workingGamma->ApproximateDecodingGamma() * gamma->ApproximateDecodingGamma()));
#endif // PNG_WRITE_gAMA_SUPPORTED

#if defined(PNG_WRITE_sRGB_SUPPORTED)
    if (options.encodingGamma && typeid(*options.encodingGamma) == typeid(SRGBGammaCurve))
        // gamma curve is sRGB transfer function; pretend that color space also matches sRGB
        png_set_sRGB(png_ptr, info_ptr, PNG_sRGB_INTENT_PERCEPTUAL); // we have to write *some* value; perceptual will probably be the most common
#endif // PNG_WRITE_gAMA_SUPPORTED

#if defined(PNG_WRITE_oFFs_SUPPORTED)
    png_set_oFFs(png_ptr, info_ptr, options.offset_x, options.offset_y, PNG_OFFSET_PIXEL);
#endif // PNG_WRITE_oFFs_SUPPORTED

#if defined(PNG_WRITE_tIME_SUPPORTED)
    png_time timestamp;
    timestamp.year   = meta.getYear();
    timestamp.month  = meta.getMonth();
    timestamp.day    = meta.getDay();
    timestamp.hour   = meta.getHour();
    timestamp.minute = meta.getMin();
    timestamp.second = meta.getSec();
    png_set_tIME(png_ptr, info_ptr, &timestamp);
#endif // PNG_WRITE_tIME_SUPPORTED

#if defined(PNG_WRITE_TEXT_SUPPORTED)
    /* Line feed is "local" */
    string comment=string("Render Date: ") + meta.getDateTime() + "\n";
    if (!meta.getComment1().empty())
        comment += meta.getComment1() + "\n";
    if (!meta.getComment2().empty())
        comment += meta.getComment2() + "\n";
    if (!meta.getComment3().empty())
        comment += meta.getComment3() + "\n";
    if (!meta.getComment4().empty())
        comment += meta.getComment4() + "\n";
    string soft (meta.getSoftware());
    png_text p_text[2] = {
        { PNG_TEXT_COMPRESSION_NONE, const_cast<char *>("Software"), const_cast<char *>(soft.c_str()),    soft.length() },
        { PNG_TEXT_COMPRESSION_NONE, const_cast<char *>("Comment"),  const_cast<char *>(comment.c_str()), comment.length() },
    };
    png_set_text(png_ptr, info_ptr, p_text, 2);
#endif // PNG_WRITE_TEXT_SUPPORTED

    png_write_info(png_ptr, info_ptr);
    png_stride = use_color ? 3 : 1;
    if (use_alpha)
        png_stride++;
    png_stride *= (bpcc + 7) / 8;

    boost::scoped_array<png_byte> row_ptr (new png_byte [width*png_stride]);

    for (int row = 0 ; row < height ; row++)
    {
        /*
        * We must copy all the values because PNG expects RGBRGB bytes, but
        * POV-Ray stores RGB components in separate arrays as floats.  In
        * order to use the full scale values at the lower bit depth, PNG
        * recommends filling the low-order bits with a copy of the high-order
        * bits.  However, studies have shown that filling the low order bits
        * with constant bits significantly improves compression, which we're
        * doing here.  Note that since the true bit depth is stored in the
        * sBIT chunk, the extra packed bits are not important.
        */

        switch (bpcc)
        {
            case 5:
            case 6:
            case 7:
                // Handle shifting for arbitrary output bit depth
                hiShift = 8 - bpcc;
                loShift = 2*bpcc - 8;
                if (use_color)
                {
                    for (int col = j = 0; col < width; col++, j += png_stride)
                    {
                        if (use_alpha)
                        {
                            GetEncodedRGBAValue (image, col, row, gamma, maxValue, r, g, b, alpha, *dither, premul);
                            row_ptr[j + 3] = alpha << hiShift;
                            row_ptr[j + 3] |= alpha >> loShift;
                        }
                        else
                            GetEncodedRGBValue (image, col, row, gamma, maxValue, r, g, b, *dither);

                        row_ptr[j] = r << hiShift;
                        row_ptr[j] |= r >> loShift;

                        row_ptr[j+1] = g << hiShift;
                        row_ptr[j+1] |= g >> loShift;

                        row_ptr[j+2] = b << hiShift;
                        row_ptr[j+2] |= b >> loShift;
                    }
                }
                else
                {
                    for (int col = j = 0; col < width; col++, j += png_stride)
                    {
                        if (use_alpha)
                        {
                            GetEncodedGrayAValue (image, col, row, gamma, maxValue, color, alpha, *dither, premul);
                            row_ptr[j + 1] = alpha << hiShift;
                            row_ptr[j + 1] |= alpha >> loShift;
                        }
                        else
                            color = GetEncodedGrayValue (image, col, row, gamma, maxValue, *dither) ;

                        // Use left-bit replication (LBR) for bit depths < 8
                        row_ptr[j] = color << hiShift;
                        row_ptr[j] |= color >> loShift;
                    }
                }
                break;

            case 8:
                if (use_color)
                {
                    for (int col = j = 0; col < width; col++, j += png_stride)
                    {
                        if (use_alpha)
                        {
                            GetEncodedRGBAValue (image, col, row, gamma, maxValue, r, g, b, alpha, *dither, premul);
                            row_ptr[j + 3] = alpha;
                        }
                        else
                            GetEncodedRGBValue (image, col, row, gamma, maxValue, r, g, b, *dither) ;
                        row_ptr[j] = r;
                        row_ptr[j + 1] = g;
                        row_ptr[j + 2] = b;
                    }
                }
                else
                {
                    for (int col = j = 0; col < width; col++, j += png_stride)
                    {
                        if (use_alpha)
                        {
                            GetEncodedGrayAValue (image, col, row, gamma, maxValue, color, alpha, *dither, premul);
                            row_ptr[j + 1] = alpha;
                        }
                        else
                            color = GetEncodedGrayValue (image, col, row, gamma, maxValue, *dither) ;
                        row_ptr[j] = color;
                    }
                }
                break;

            case 16:
                if (use_color)
                {
                    for (int col = j = 0; col < width; col++, j += png_stride)
                    {
                        if (use_alpha)
                        {
                            GetEncodedRGBAValue (image, col, row, gamma, maxValue, r, g, b, alpha, *dither, premul);
                            row_ptr[j+6] = alpha >> 8;
                            row_ptr[j+7] = alpha & 0xff;
                        }
                        else
                            GetEncodedRGBValue (image, col, row, gamma, maxValue, r, g, b, *dither) ;

                        row_ptr[j] = r >> 8;
                        row_ptr[j + 1] = r & 0xFF;

                        row_ptr[j + 2] = g >> 8;
                        row_ptr[j + 3] = g & 0xFF;

                        row_ptr[j + 4] = b >> 8;
                        row_ptr[j + 5] = b & 0xFF;
                    }
                }
                else
                {
                    for (int col = j = 0; col < width; col++, j += png_stride)
                    {
                        if (use_alpha)
                        {
                            GetEncodedGrayAValue (image, col, row, gamma, maxValue, color, alpha, *dither, premul);
                            row_ptr[j+2] = alpha >> 8;
                            row_ptr[j+3] = alpha & 0xff;
                        }
                        else
                            color = GetEncodedGrayValue (image, col, row, gamma, maxValue, *dither) ;
                        row_ptr[j] = color >> 8;
                        row_ptr[j+1] = color & 0xff;
                    }
                }
                break;

            default:  // bpcc 9 - 15
                // Handle shifting for arbitrary output bit depth
                hiShift = 16 - bpcc;
                loShift = 2*bpcc - 16;
                himask = 0xFF ^ ((1 << hiShift) - 1);
                if (use_color)
                {
                    for (int col = j = 0; col < width; col++, j += png_stride)
                    {
                        if (use_alpha)
                        {
                            GetEncodedRGBAValue (image, col, row, gamma, maxValue, r, g, b, alpha, *dither, premul);
                            row_ptr[j + 6] = alpha >> (8 - hiShift);
                            row_ptr[j + 7] = alpha << hiShift;
                            row_ptr[j + 7] |= alpha >> loShift;
                        }
                        else
                            GetEncodedRGBValue (image, col, row, gamma, maxValue, r, g, b, *dither) ;

                        row_ptr[j] = r >> (8 - hiShift);
                        row_ptr[j + 1] = r << hiShift;
                        row_ptr[j + 1] |= r >> loShift;

                        row_ptr[j + 2] = g >> (8 - hiShift);
                        row_ptr[j + 3] = g << hiShift;
                        row_ptr[j + 3] |= g >> loShift;

                        row_ptr[j + 4] = b >> (8 - hiShift);
                        row_ptr[j + 5] = b << hiShift;
                        row_ptr[j + 5] |= b >> loShift;
                    }
                }
                else
                {
                    for (int col = j = 0; col < width; col++, j += png_stride)
                    {
                        if (use_alpha)
                        {
                            GetEncodedGrayAValue (image, col, row, gamma, maxValue, color, alpha, *dither, premul);
                            row_ptr[j + 2] = alpha >> (8 - hiShift);
                            row_ptr[j + 3] = alpha << hiShift;
                            row_ptr[j + 3] |= alpha >> loShift;
                        }
                        else
                            color = GetEncodedGrayValue (image, col, row, gamma, maxValue, *dither) ;

                        row_ptr[j] = color >> (8 - hiShift);
                        row_ptr[j + 1] = color << hiShift;
                        row_ptr[j + 1] |= color >> loShift;
                    }
                }
        }

        if (setjmp(png_jmpbuf(png_ptr)))
        {
            if (messages.error.length() > 0)
                throw POV_EXCEPTION(kFileDataErr, messages.error.c_str());

            // If we get here, we had a problem writing the file
            throw POV_EXCEPTION(kFileDataErr, "Cannot write PNG output data");
        }

        // Write out a scanline
        png_write_row (png_ptr, row_ptr.get());
    }

    if (messages.error.length() > 0)
        throw messages.error.c_str();

    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
}

} // end of namespace Png

}

#endif  // LIBPNG_MISSING
