#include "return_codes.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ZLIB)
#include <zlib.h>
#elif defined(LIBDEFLATE)
#include <libdeflate.h>
int uncompress(unsigned char *data, size_t *ptr_new_length, unsigned char *compressed_data, size_t length)
{
	struct libdeflate_decompressor *decompressor = libdeflate_alloc_decompressor();
	if (!decompressor)
	{
		return ERROR_UNKNOWN;
	}
	int result = libdeflate_zlib_decompress(decompressor, compressed_data, length, data, *ptr_new_length, ptr_new_length);
	libdeflate_free_decompressor(decompressor);
	return result;
}
#elif defined(ISAL)
#include <include/igzip_lib.h>
int uncompress(unsigned char *data, size_t *ptr_new_length, unsigned char *compressed_data, size_t length)
{
	struct inflate_state state;
	isal_inflate_init(&state);
	state.next_in = compressed_data;
	state.avail_in = length;
	state.next_out = data;
	state.avail_out = *ptr_new_length;
	state.crc_flag = ISAL_ZLIB;
	while (state.avail_in > 0)
	{
		int ret = isal_inflate(&state);
		if (ret != ISAL_DECOMP_OK)
		{
			return ret;
		}
	}
	*ptr_new_length = state.total_out;
	isal_inflate_reset(&state);
	return SUCCESS;
}
#endif

int checkSignature(FILE *in)
{
	unsigned char signature[8];
	int frsignature = fread(signature, 1, 8, in);
	if (frsignature != 8)
	{
		return ERROR_DATA_INVALID;
	}
	unsigned char truesignature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	for (size_t i = 0; i < 8; i++)
	{
		if (signature[i] != truesignature[i])
		{
			return ERROR_DATA_INVALID;
		}
	}
	return SUCCESS;
}

size_t check_plte_type(unsigned char *plte, size_t plte_length)
{
	for (size_t i = 0; i < plte_length; i += 3)
	{
		if (plte[i] != plte[i + 1] || plte[i + 1] != plte[i + 2])
		{
			return 2;
		}
	}
	return 1;
}

size_t count_byte(unsigned char type, int plte_type)
{
	switch (type)
	{
	case 0:
		return 1;
	case 2:
		return 3;
	case 3:
		if (plte_type == 1)
		{
			return 1;
		}
		return 3;
	default:
		return 0;
	}
}

unsigned char up(unsigned char *raw_data, size_t n, size_t width)
{
	if (n < width)
	{
		return 0;
	}
	return raw_data[n - width];
}

unsigned char upperleft(unsigned char *raw_data, size_t i, size_t n, size_t width, size_t bytes_count)
{
	if (n < width * i + bytes_count || n < width + bytes_count)
	{
		return 0;
	}
	return raw_data[n - width - bytes_count];
}

unsigned char recon0(unsigned char *raw_data, size_t i, size_t n, size_t width, size_t bytes_count)
{
	return 0;
}

// now sub
unsigned char recon1(unsigned char *raw_data, size_t i, size_t n, size_t width, size_t bytes_count)
{
	if (n < i * width + bytes_count)
	{
		return 0;
	}
	return raw_data[n - bytes_count];
}

unsigned char recon2(unsigned char *raw_data, size_t i, size_t n, size_t width, size_t bytes_count)
{
	return up(raw_data, n, width);
}

unsigned char recon3(unsigned char *raw_data, size_t i, size_t n, size_t width, size_t bytes_count)
{
	return (recon1(raw_data, i, n, width, bytes_count) + up(raw_data, n, width)) / 2;
}

unsigned char recon4(unsigned char *raw_data, size_t i, size_t n, size_t width, size_t bytes_count)
{
	int a = recon1(raw_data, i, n, width, bytes_count), b = up(raw_data, n, width), c = upperleft(raw_data, i, n, width, bytes_count);
	int p = a + b - c;
	int pa = abs(p - a);
	int pb = abs(p - b);
	int pc = abs(p - c);
	if (pa <= pb && pa <= pc)
	{
		return a;
	}
	else if (pb <= pc)
	{
		return b;
	}
	return c;
}

int reconstraction(unsigned char *raw_data, unsigned char *data, size_t length, size_t heigth, size_t width, size_t bytes_count, unsigned char *plte, size_t plte_type)
{
	size_t len = width * bytes_count;
	size_t k = length / (len + 1);
	unsigned char (*recon)(unsigned char *arr, size_t i, size_t x, size_t width, size_t bytes_count);
	size_t tmp = 0;
	for (size_t i = 0; i < k; i++)
	{
		switch (data[i + i * len])
		{
		case 0:
			recon = recon0;
			break;
		case 1:
			recon = recon1;
			break;
		case 2:
			recon = recon2;
			break;
		case 3:
			recon = recon3;
			break;
		case 4:
			recon = recon4;
			break;
		default:
			return ERROR_DATA_INVALID;
		}
		for (size_t j = 0; j < len; j++)
		{
			if (!plte_type)
			{
				raw_data[j + i * len] = data[j + i + i * len + 1] + recon(raw_data, i, j + i * len, len, bytes_count);
			}
			else
			{
				tmp = data[j + i + i * len + 1] + recon(raw_data, i, j + i * len, len, bytes_count);
				if (plte_type == 1)
				{
					raw_data[j + i * len] = plte[tmp * 3];
				}
				else
				{
					raw_data[(j + i * len) * 3] = plte[tmp * 3];			// R
					raw_data[(j + i * len) * 3 + 1] = plte[tmp * 3 + 1];	// G
					raw_data[(j + i * len) * 3 + 2] = plte[tmp * 3 + 2];	// B
				}
			}
		}
	}
	return SUCCESS;
}

int readPLTE(FILE *in, unsigned char *plte, size_t length)
{
	if (length % 3 != 0)
	{
		fprintf(stderr, "PLTE chuck length must be divisible by 3");
		return ERROR_DATA_INVALID;
	}
	if (fread(plte, 1, length, in) != length)
	{
		fprintf(stderr, "Failed reading PLTE chunk");
		return ERROR_DATA_INVALID;
	}
	return SUCCESS;
}

int read_other(FILE *in, size_t length)
{
	return fseek(in, length, SEEK_CUR);	   // Just skip
}

int readIDAT(FILE *in, unsigned char *data, size_t length)
{
	if (fread(data, 1, length, in) != length)
	{
		fprintf(stderr, "Failed reading IDAT chunk");
		return ERROR_DATA_INVALID;
	}
	return SUCCESS;
}

int readCRC(FILE *in, unsigned char *buf)
{
	if (fread(buf, 1, 4, in) != 4)
	{
		fprintf(stderr, "Failed reading CRC chunk");
		return ERROR_DATA_INVALID;
	}
	return SUCCESS;
}

size_t array_to_int(unsigned char *arr)
{
	if (arr[0] > 127)
	{
		return INT_MAX;
	}
	return (arr[0] << 24) + (arr[1] << 16) + (arr[2] << 8) + arr[3];
}

int check_end(FILE *f)
{
	return getc(f) == -1;
}

int main(int argc, const char *argv[])
{
	int result = SUCCESS;
	FILE *in = NULL, *out = NULL;
	unsigned char *compressed_data = NULL;
	unsigned char *data = NULL;
	unsigned char *new_data = NULL;
	unsigned char *plte = NULL;

	if (argc != 3)
	{
		fprintf(stderr, "Expected two arguments: input file name and output file name.");
		result = ERROR_PARAMETER_INVALID;
		goto cleanUp;
	}
	in = fopen(argv[1], "rb");
	if (!in)
	{
		fprintf(stderr, "Failed to open file: %s", argv[1]);
		result = ERROR_CANNOT_OPEN_FILE;
		goto cleanUp;
	}
	result = checkSignature(in);
	if (result != SUCCESS)
	{
		fprintf(stderr, "The file format is not png");
		goto cleanUp;
	}
	unsigned char ihdrlength[4];
	if (fread(&ihdrlength, 1, 4, in) != 4)
	{
		fprintf(stderr, "Failed reading IDHR length");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	if (array_to_int(ihdrlength) != 13)
	{
		fprintf(stderr, "Wrong IHDR");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	char ihdrname[5];
	ihdrname[4] = 0;
	if (fread(ihdrname, 1, 4, in) != 4)
	{
		fprintf(stderr, "Failed reading IDHR name");
		result = ERROR_UNSUPPORTED;
		goto cleanUp;
	}
	if (strcmp(ihdrname, "IHDR"))
	{
		fprintf(stderr, "IHDR must be the first chunk");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	unsigned char arrwidth[4];
	if (fread(arrwidth, 1, 4, in) != 4)
	{
		fprintf(stderr, "Failed to read image width");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	size_t width = array_to_int(arrwidth);
	if (width >= INT_MAX)
	{
		fprintf(stderr, "Invalid image width");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	unsigned char arrheight[4];
	if (fread(arrheight, 1, 4, in) != 4)
	{
		fprintf(stderr, "Failed to read image height");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	size_t height = array_to_int(arrheight);
	if (height >= INT_MAX)
	{
		fprintf(stderr, "Invalid image height");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	unsigned char bitdepth;
	if (fread(&bitdepth, 1, 1, in) != 1)
	{
		fprintf(stderr, "Failed reading bitdetph");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	if (bitdepth != 8)
	{
		fprintf(stderr, "File with bitdetph %d is not supported", bitdepth);
		result = ERROR_UNSUPPORTED;
		goto cleanUp;
	}
	unsigned char colourtype;
	if (fread(&colourtype, 1, 1, in) != 1)
	{
		fprintf(stderr, "Failed reading Colourtype");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	if (!(colourtype == 0 || colourtype == 2 || colourtype == 3))
	{
		fprintf(stderr, "Colourtype %d is not supported, only 0, 2 or 3", colourtype);
		result = ERROR_UNSUPPORTED;
		goto cleanUp;
	}
	unsigned char compretionmethod;
	if (fread(&compretionmethod, 1, 1, in) != 1)
	{
		fprintf(stderr, "Failed reading compression code");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	if (compretionmethod != 0)
	{
		fprintf(stderr, "Unsupported compression code %d", compretionmethod);
		result = ERROR_UNSUPPORTED;
		goto cleanUp;
	}
	unsigned char filtermethod;
	if (fread(&filtermethod, 1, 1, in) != 1)
	{
		fprintf(stderr, "Failed reading filter code");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	if (filtermethod)
	{
		fprintf(stderr, "Unsupported filter code %d", filtermethod);
		result = ERROR_UNSUPPORTED;
		goto cleanUp;
	}
	unsigned char interlacemethod;
	if (fread(&interlacemethod, 1, 1, in) != 1)
	{
		fprintf(stderr, "Failed reading interplace");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	if (interlacemethod)
	{
		fprintf(stderr, "Interplace is not supported");
		result = ERROR_UNSUPPORTED;
		goto cleanUp;
	}
	unsigned char buf[5];
	result = readCRC(in, buf);
	if (result != SUCCESS)
	{
		goto cleanUp;
	}
	size_t countidat = 0;
	size_t countiend = 0;
	size_t countplte = 0;
	int chunk_after_idat = 0;
	int plte_type = 0;	  // 0 - none, 1 - gray, 2 - rgb
	size_t length = 0;
	while (!countiend && !feof(in))
	{
		unsigned char arrchunklength[4];
		if (fread(arrchunklength, 1, 4, in) != 4)
		{
			fprintf(stderr, "Failed reading chunk length");
			result = ERROR_DATA_INVALID;
			goto cleanUp;
		}
		size_t chunk_length = array_to_int(arrchunklength);
		if (chunk_length >= INT_MAX)
		{
			fprintf(stderr, "Failed reading chunk length");
			result = ERROR_DATA_INVALID;
			goto cleanUp;
		}
		char type[5];
		type[4] = 0;
		if (fread(type, 1, 4, in) != 4)
		{
			fprintf(stderr, "Failed reading chunk type");
			result = ERROR_DATA_INVALID;
			goto cleanUp;
		}
		if (!strcmp(type, "PLTE"))
		{
			if (colourtype == 0)
			{
				fprintf(stderr, "PLTE not allowed in grayscale image");
				result = ERROR_DATA_INVALID;
				goto cleanUp;
			}
			plte = malloc(sizeof(unsigned char) * chunk_length);
			if (!plte)
			{
				fprintf(stderr, "Failed allocate memory");
				result = ERROR_OUT_OF_MEMORY;
				goto cleanUp;
			}
			result = readPLTE(in, plte, chunk_length);
			if (result != SUCCESS)
			{
				goto cleanUp;
			}
			plte_type = check_plte_type(plte, chunk_length);
			++countplte;
		}
		else if (!strcmp(type, "IEND"))
		{
			countiend++;
		}
		else if (!strcmp(type, "IDAT"))
		{
			if (chunk_after_idat)
			{
				fprintf(stderr, "IDAT chunks must appear consecutively");
				result = ERROR_DATA_INVALID;
				goto cleanUp;
			}
			if (!countidat)
			{
				compressed_data = malloc(sizeof(unsigned char) * (chunk_length));
				if (!compressed_data)
				{
					fprintf(stderr, "Failed allocate memory");
					result = ERROR_OUT_OF_MEMORY;
					goto cleanUp;
				}
			}
			else
			{
				compressed_data = realloc(compressed_data, sizeof(unsigned char) * (length + chunk_length));
				if (!compressed_data)
				{
					fprintf(stderr, "Failed allocate memory");
					result = ERROR_OUT_OF_MEMORY;
					goto cleanUp;
				}
			}
			countidat++;
			result = readIDAT(in, compressed_data + length, chunk_length);
			if (result != SUCCESS)
			{
				goto cleanUp;
			}
			length += chunk_length;
		}
		else
		{
			if (countidat)
			{
				chunk_after_idat = 1;
			}
			result = read_other(in, chunk_length);
			if (result != SUCCESS)
			{
				fprintf(stderr, "Failed reading %s chunk", type);
				result = ERROR_DATA_INVALID;
				goto cleanUp;
			}
		}

		result = readCRC(in, buf);
		if (result != SUCCESS)	  // CRC
		{
			goto cleanUp;
		}
	}
	if (!countidat || !countiend || !check_end(in))
	{
		fprintf(stderr, "None IDAT or none IEND");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	if (colourtype == 3 && countplte != 1)
	{
		fprintf(stderr, "There must be one PLTE chunk in colourtype 3");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	if (countplte > 1)
	{
		fprintf(stderr, "There must be only one PLTE chunk");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}
	size_t new_length = height + height * width * count_byte(colourtype, 1);
	size_t *ptr_new_length = &new_length;
	data = malloc(sizeof(unsigned char) * new_length);
	if (!data)
	{
		fprintf(stderr, "Failed allocate memory");
		result = ERROR_OUT_OF_MEMORY;
		goto cleanUp;
	}
	int endf = uncompress(data, ptr_new_length, compressed_data, length);
	if (endf != SUCCESS)
	{
		fprintf(stderr, "Failed to uncompress");
		result = ERROR_DATA_INVALID;
		goto cleanUp;
	}

	free(compressed_data);
	compressed_data = NULL;

	new_data = malloc(sizeof(unsigned char) * height * width * count_byte(colourtype, plte_type));

	if (!new_data)
	{
		fprintf(stderr, "Failed allocate memory");
		result = ERROR_OUT_OF_MEMORY;
		goto cleanUp;
	}

	size_t bytes_count = count_byte(colourtype, 1);

	result = reconstraction(new_data, data, new_length, height, width, bytes_count, plte, plte_type);
	if (result != SUCCESS)
	{
		fprintf(stderr, "Reconstraction in not successful");
		goto cleanUp;
	}
	out = fopen(argv[2], "wb");
	if (!out)
	{
		fprintf(stderr, "Failed to open output file");
		result = ERROR_CANNOT_OPEN_FILE;
		goto cleanUp;
	}

	unsigned char pnmtype;
	if ((colourtype == 0) || (colourtype == 3 && plte_type == 1))
	{
		pnmtype = '5';
	}
	else
	{
		pnmtype = '6';
	}
	fprintf(out, "P%c\n%zu %zu\n255\n", pnmtype, width, height);
	fwrite(new_data, 1, sizeof(unsigned char) * height * width * count_byte(colourtype, plte_type), out);
cleanUp:
	free(compressed_data);
	free(data);
	free(new_data);
	free(plte);
	if (in)
		fclose(in);
	if (out)
		fclose(out);
	return result;
}
