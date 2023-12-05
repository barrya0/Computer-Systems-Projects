
#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <cuda.h>
#include <ctime>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

int width, height; //image dimensions

//3x3 matrix for convolution
int mask[3][3] = { 1, 2, 1, 
				2, 3, 2, 
				1, 2, 1 };

//get pixel value after applying convolution filter
int Pixelval(unsigned char* arr, int col, int row) {
	//stores convolution result
	int sum = 0;
	for (int j = -1; j <= 1; j++) {
		for (int i = -1; i <= 1; i++) {
			//use mask to get pixel value from array
			int c = arr[(row + j) * width + (col + i)];
			//convolution operation
			sum += c * mask[i + 1][j + 1];
		}
	}
	sum = sum / 15; //normalize the sum
	return sum;
}

//Function that serves the host (CPU) to apply Blur effect sequentially
void hostBlur(unsigned char* arr, unsigned char* result) {
	int offset = 2 * width;
	//excluding 1st and last 3 rows
	for (int row = 2; row < height - 3; row++) {
		//exclude 1st and last 3 columns
		for (int col = 2; col < width - 3; col++) {
			//apply blur effect with Pixelval
			result[offset + col] = Pixelval(arr, col, row);
		}
		offset += width;
	}
}

// Function to load an image using stb_image
void loadImage(const char* filePath, unsigned char** data, int* w, int* h, int* channels) {
	*data = stbi_load(filePath, w, h, channels, 0);
	if (!*data) {
		fprintf(stderr, "Error: Couldn't load the image: %s\n", filePath);
		exit(EXIT_FAILURE);
	}
}

// Function to write an image using stb_image_write
void writeImage(const char* filePath, unsigned int w, unsigned int h, int channels, unsigned char* data) {
	if (!stbi_write_png(filePath, w, h, channels, data, w * channels)) {
		fprintf(stderr, "Error: Couldn't write the image: %s\n", filePath);
		exit(EXIT_FAILURE);
	}
}


// Function to load a PGM file and return image dimensions and pixel data
void loadPGM(const char* filePath, unsigned int* width, unsigned int* height, unsigned char** data) {
	FILE* file = fopen(filePath, "rb");
	if (!file) {
		fprintf(stderr, "Error: Couldn't open the file: %s\n", filePath);
		exit(EXIT_FAILURE);
	}

	// Read PGM header
	char magic[3];
	fscanf(file, "%2s", magic);
	if (strcmp(magic, "P5") != 0) {
		fprintf(stderr, "Error: Not a PGM file (magic number P5 expected)\n");
		exit(EXIT_FAILURE);
	}

	fscanf(file, "%u %u", width, height);
	fgetc(file); // Ignore one whitespace character
	unsigned int maxValue;
	fscanf(file, "%u", &maxValue);

	// Allocate memory for pixel data
	*data = (unsigned char*)malloc((*width) * (*height));

	// Read pixel data
	fread(*data, sizeof(unsigned char), (*width) * (*height), file);

	fclose(file);
}

// Function to write PGM image to a file
void writePGM(const char* filePath, unsigned int width, unsigned int height, unsigned char* data) {
	FILE* file = fopen(filePath, "wb");
	if (!file) {
		fprintf(stderr, "Error: Couldn't open the file for writing: %s\n", filePath);
		exit(EXIT_FAILURE);
	}

	// Write PGM header
	fprintf(file, "P5\n%d %d\n255\n", width, height);

	// Write pixel data
	fwrite(data, sizeof(unsigned char), width * height, file);

	fclose(file);
}

//Function that serves the device (GPU) to apply Blur effect with parallelism
__global__ void deviceBlur(unsigned char* arr, unsigned char* result, int width, int height) {
	//Calculate column and row indices for each thread
	int col = blockIdx.x * blockDim.x + threadIdx.x;
	int row = blockIdx.y * blockDim.y + threadIdx.y;

	//Is current thread outside of valid image area?
	if (row < 2 || col < 2 || row >= height - 3 || col >= width - 3)
		return;

	//Same process as seen in Pixelval function
	//define blur mask within kernel
	int mask[3][3] = {
		{1,2,1},
		{2,3,2},
		{1,2,1}
	};
	int sum = 0;
	for (int j = -1; j <= 1; j++) {
		for (int i = -1; i <= 1; i++) {
			//use mask to get pixel value from array
			int c = arr[(row + j) * width + (col + i)];
			//convolution operation
			sum += c * mask[i + 1][j + 1];
		}
	}
	result[row * width + col] = sum / 15;
}

// Function to apply blur using CUDA
void blurUsingCUDA(unsigned char* d_arr, unsigned char* d_result, unsigned int width, unsigned int height) {
	// Set up the grid and block dimensions
	dim3 threadsPerBlock(16, 16);
	//dim3 numBlocks(width/16, height/16); may work better only with PGMS
	dim3 numBlocks((width + threadsPerBlock.x - 1) / threadsPerBlock.x, (height + threadsPerBlock.y - 1) / threadsPerBlock.y);

	// Launch the CUDA kernel
	deviceBlur<<<numBlocks, threadsPerBlock>>>(d_arr, d_result, width, height);

	// Ensure all threads have finished
	cudaDeviceSynchronize();
}

int main(int argc, char** argv) {
	unsigned char* h_res;
	unsigned char* d_res;
	unsigned char* hPix = NULL;
	unsigned char* dPix = NULL;

	const char* image = "C:/Users/abdou/source/repos/GaussianBlur_ACSYS_projfinal_p1/images/3200x2400.png";
	const char* hostResultPath = "C:/Users/abdou/source/repos/GaussianBlur_ACSYS_projfinal_p1/result/hBlur3200x2400.png";
	const char* deviceResultPath = "C:/Users/abdou/source/repos/GaussianBlur_ACSYS_projfinal_p1/result/dBlur3200x2400.png";

	//populate data from pgm onto host
	//loadPGM(image, &width, &height, &hPix);
	int channels;
	hPix = stbi_load(image, &width, &height, &channels, 0);
	if (!hPix) {
		fprintf(stderr, "Error: Couldn't load the image: %s\n", image);
		exit(EXIT_FAILURE);
	}

	int imageSize = sizeof(unsigned char) * width * height * channels;

	h_res = (unsigned char*)malloc(imageSize);
	cudaMalloc((void**)&dPix, imageSize);
	cudaMalloc((void**)&d_res, imageSize);
	cudaMemcpy(dPix, hPix, imageSize, cudaMemcpyHostToDevice);

	clock_t start, end, elapsed;
	start = clock();

	hostBlur(hPix, h_res);
	end = clock();
	elapsed = (end - start);
	double diff = elapsed / (double)CLOCKS_PER_SEC;
	printf("CPU time = %.2f ms\n", diff * 1000);

	//writePGM(hostResultPath, width, height, h_res);
	writeImage(hostResultPath, width, height, channels, h_res);

	start = clock();
	//Invoke the kernel
	blurUsingCUDA(dPix, d_res, width, height);
	end = clock();
	elapsed = (end - start);
	diff = elapsed / (double)CLOCKS_PER_SEC;
	printf("GPU time = %.2f ms\n", diff * 1000);

	cudaMemcpy(h_res, d_res, imageSize, cudaMemcpyDeviceToHost);

	//writePGM(deviceResultPath, width, height, h_res);
	writeImage(deviceResultPath, width, height, channels, h_res);

	//Free allocated memory
	cudaFree(dPix);
	cudaFree(d_res);
	stbi_image_free(hPix);
	free(h_res);
	return 0;
}