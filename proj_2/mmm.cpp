/*The Purpose of this program is to perform matrix-matrix multiplication using various methods and compare them to the native implementation of matrix-matrix multiplication.
(i) Multiple threads
(ii) x86 SIMD Instructions
(iii) Cache miss optimization
(iv) All 3 together
(v) Three 1+1 combinations of the 3 techniques
The implementation supports (1) configurable matrix size that can be much larger than the on-chip cache capacity, and (2) both fixed-point and floating-point data. Moreover, the program allows users to individually turn on/off the three optimization techniques (i.e., multi-threading, SIMD, and cache miss 
minimization) and configure the thread number.
Author : Abdoula Barry
DOC : 10/09/2023
*/
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <string>
#include <sstream>
#include <stdexcept>
#include <random>
#include <cstdint> //For fixed-width integer types
#include <chrono>
#include <immintrin.h> //Include for AVX2 intrinsics - SIMD

using namespace std;

const int simd_width = 8;	// AVX2 SIMD width

mutex mtx; //Mutex for synchronization

//Creating a templated Matrix Class to accomodate varying data types
template <typename T>
class Matrix{
public:
	Matrix(int rows, int cols) : rows_(rows), cols_(cols), data_(rows, std:: vector<T>(cols)) {}

	T& operator()(int row, int col){
		return data_[row][col];
	}
	const T& operator()(int row, int col) const {
		return data_[row][col];
	}
	int getRows() const {
		return rows_;
	}
	int getCols() const {
		return cols_;
	}
private:
	int rows_;
	int cols_;
	vector<vector<T>> data_;
};

// Function to populate a matrix with random float values
template <typename T>
void populateMatrixWithRandomFloat(Matrix<T>& matrix) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<T> dist(0.0, 1.0);

    for (int i = 0; i < matrix.getRows(); i++) {
        for (int j = 0; j < matrix.getCols(); j++) {
            matrix(i, j) = static_cast<T>(dist(gen));  // Generate a random float value
        }
    }
}
//Function to populate matrix with random fixed points
template <typename T>
void populateMatrixWithRandomFixedPoint(Matrix<T>& matrix, T min, T max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(0.0, 1.0); // Use double as a floating-point type

    for (int i = 0; i < matrix.getRows(); i++) {
        for (int j = 0; j < matrix.getCols(); j++) {
            double randomValue = dist(gen);
            matrix(i, j) = static_cast<T>(min + randomValue * (max - min)); // Map to fixed-point range
        }
    }
}
//Function to transpose a given matrix
template <typename T>
void transpose(Matrix<T>& B){
	for(int i = 0; i < B.getRows(); i++){
		for(int j = 0; j < B.getCols(); j++){
			B(j, i) = B(i, j);
		}
	}
}

//Basic method to multiply 2 matrices or submatrices using templated class
template <typename T>
void basicMatrixMultiply(const Matrix<T>& A, const Matrix<T>& B, Matrix<T>& C,
						int start, int end){

	for(int i = start; i < end; i++){
		for(int j = 0; j < A.getCols(); j++){
			T sum = 0;
			//This loop calculates the dot product
			for(int k = 0; k < A.getCols(); k++){
				sum += A(i, k) * B(k, j);
			}
			mtx.lock();		//lock mutex before updating result
			C(i, j) = sum;	//Store value in result
			mtx.unlock();	//unlock
		}
	}
}

//Function that performs matrix-matrix multiplication using SIMD(Single Instruction, Multiple Data). Performs the same operation on multipledata points simultaneously
template <typename T>
void SIMD(const Matrix<T>& A, const Matrix<T>& B, Matrix<T>& C, int start, int end){
	for (int i = start; i < end; i++) {
		for (int j = 0; j < A.getCols(); j++){
			__m256i sum = _mm256_setzero_si256();
			for (int k = 0; k < A.getCols(); k += 8) {
				__m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&A(i, k)));
				__m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&B(k, j)));
				sum = _mm256_add_epi32(sum, _mm256_mullo_epi32(a, b));
            }
			C(i, j) = _mm256_extract_epi32(sum, 0) +
				_mm256_extract_epi32(sum, 1) +
				_mm256_extract_epi32(sum, 2) +
				_mm256_extract_epi32(sum, 3) +
				_mm256_extract_epi32(sum, 4) +
				_mm256_extract_epi32(sum, 5) +
				_mm256_extract_epi32(sum, 6) +
				_mm256_extract_epi32(sum, 7);
		}
    }
}

//Function that uses multi-threading to separate matrix-matrix multiplication into blocks that are handled by threads concurrently
template <typename T>
void multiThreading(const Matrix<T>& A, const Matrix<T>& B, Matrix<T>& C, int numThreads, bool simd){
	thread threads[numThreads];	// Create threads
	int blockSize = A.getRows() / numThreads;
	int start = 0;
	int end = 0;
	
	//Start the threads for matrix multiplication
	for(int i = 0; i < numThreads; i++){
		start = i * blockSize;
		end = (i == numThreads - 1) ? A.getRows() : start + blockSize;
		//checking which method to use with the threads
		if(simd){
			//threading and SIMD
			threads[i] = thread([&, start, end](){SIMD(A, B, C, start, end);});
		}
		else{ 
			//threading and basic matrix multiplication
			threads[i] = thread([&, start, end](){basicMatrixMultiply(A, B, C, start, end);}); //Using lambda function to pass matrixMultiply to thread
		}
	}
	//join the threads
	for(int i = 0; i < numThreads; i++){
		threads[i].join();
	}
}

//Function to setup and handle all of the possible input combinations or experiment trials given/desired by the user. Runs time analysis in nanoseconds on each trial and prints.
template <typename T>
void performMatrixMultiplication(
    const Matrix<T>& A, Matrix<T>& B, Matrix<T>& C,
    int row, int column,
    bool cacheMissOp, bool multiThread, bool simd, int threadNum,
    bool native
) {

	// // Prints for debugging - not suitable for large matrices
	// cout << "Matrices: " << endl;
	// cout << "A" << endl;
	// for (int i = 0; i < A.getRows(); i++) {
	// 	for (int j = 0; j < A.getCols(); j++) {
	// 		cout << A(i, j) << " ";
	// 	}
	// 	cout << endl;
	// }
	// cout << "B" << endl;
	// for (int i = 0; i < B.getRows(); i++) {
	// 	for (int j = 0; j < B.getCols(); j++) {
	// 		cout << B(i, j) << " ";
	// 	}
	// 	cout << endl;
	// }

    // Common setup for all data types
    if (cacheMissOp) {
        // Transpose matrix B to improve spatial locality when performing matrix-matrix multiplication
        transpose(B);
    }

    // Common code to measure time
    auto startTime = chrono::high_resolution_clock::now();

    if (native) {
        // Perform matrix multiplication and return result matrix
		cout << "Basic Matrix-Matrix Multiplication" << endl;
        basicMatrixMultiply(A, B, C, 0, row);
    } else {
        if (multiThread && !simd && !cacheMissOp) {			//100
            cout << "MULTI-THREADING" << endl;	//Matrix Multiplication using Multithreading
            multiThreading(A, B, C, threadNum, simd);		//Call Multithreading function

        } else if (!multiThread && simd && !cacheMissOp) {	//010
            cout << "SIMD" << endl;
            SIMD(A, B, C, 0, row);							//Call SIMD function

        } else if (multiThread && simd && !cacheMissOp) {	//110
            cout << "Multi-threading & SIMD" << endl;
            multiThreading(A, B, C, threadNum, simd);		//Call Multi-threading using SIMD

        } else if (!multiThread && !simd && cacheMissOp) {	//001
            cout << "Cache optimization" << endl;
            basicMatrixMultiply(A, B, C, 0, row);			//Call basic Matrix multiplication on transposed MB matrix

        } else if (multiThread && !simd && cacheMissOp) {	//101
            cout << "Multi-threading & Cache op." << endl;
            multiThreading(A, B, C, threadNum, simd);		//Call Multithreading function on transposed matrix

        } else if (!multiThread && simd && cacheMissOp) {	//011
            cout << "SIMD & Cache op." << endl;
            SIMD(A, B, C, 0, row);							//Call simd function with transposed MB matrix

        } else if (multiThread && simd && cacheMissOp) {	//111
            cout << "All methods" << endl;
            multiThreading(A, B, C, threadNum, simd);		//Call Multithreading function to use SIMD method and transposed MB matrix
        }
    }

    // End time measurement
    auto endTime = chrono::high_resolution_clock::now();
    auto elapsed = chrono::duration_cast<chrono::nanoseconds>(endTime - startTime);

    cout << "Given:" << row << "x" << column << " This method took " << elapsed.count() << " nanoseconds" << endl;

	// // Prints for debugging
	// //Print Result Matrix
	// cout << "C - result" << endl;
	// for (int i = 0; i < C.getRows(); i++) {
	// 	for (int j = 0; j < C.getCols(); j++) {
	// 		cout << C(i, j) << " ";
	// 	}
	// 	cout << endl;
	// }

}

int main(int argc, char* argv[]) {
	if(argc < 3 || argc > 4){ 	//Program takes 3 or 4 inputs
		cerr << "Incorrect number of inputs" << endl;
		return 0;
	}
	string matrix_size = argv[1];   //First argument - Size of matrix
	int row, column;
	row = column = stoi(matrix_size);
	
	string data_type = argv[2];     //Second Argument - data type, floating, or fixed point
	
	bool native = true, multiThread = false, simd = false, cacheMissOp = false;
	int threadNum; // thread number
	if(argc == 4){ 						//4 inputs given
		string operation = argv[3];		//Third Argument - Which matrix multiplication method to use
		native = false;					//set native matrix multiplication flag to false
		if(operation.size() == 3 && operation != "000"){		//3-bit input is provided
			if(operation[0] == '1') { 
				multiThread = true;
				// Get the number of threads from the user with input validation
				while (true) {
					std::cout << "Enter the number of threads (1-" << std::thread::hardware_concurrency() << "): ";
					std::string input;
					std::getline(std::cin, input);

					std::istringstream iss(input);
					if (iss >> threadNum && threadNum >= 1 && threadNum <= std::thread::hardware_concurrency()) {
						break;
					} else {
						std::cout << "Invalid input. Please enter a valid number of threads." << std::endl;
					}
				} 
			}
			if(operation[1] == '1') {
				if(row < 104){
					cerr << "It's not practical to use SIMD on a matrix of this size. SIMD will run on matrix sizes > 104" << endl;
					return 0;
				} 
				else{
					simd = true;
					if(row % simd_width){ //If the given size is not a multiple of the SIMD width
						
						cout << "Adjusting matrix size for AVX2 instructions (if needed), which have a 256-bit (32-byte) wide SIMD register" << endl;

						int matrixSize = (row / simd_width) * simd_width; // adjust matrix size
						cout << "Adjusted size: " << matrixSize << endl;
						row = matrixSize;
						column = matrixSize;
					}
				}
			}
			if(operation[2] == '1') { cacheMissOp = true;}
		}
		else{
			cerr << "Usage: " << operation << " - must be 3_bit_input and not all 0" << endl;
			return 0;
		}
	}

	// Prints for debugging
	// cout << "native: " << native << endl;
	// cout << "thread: " << multiThread << endl;
	// cout << "simd: " << simd << endl;
	// cout << "cachemissOp: " << cacheMissOp << endl;

	if(data_type == "-2"){
		cout << "FIXED-POINT DATA" << endl;
		typedef int16_t data;    //data becomes alias for FixedPoint type

		//initialize matrices
		Matrix<data> MA(row, column);
		populateMatrixWithRandomFixedPoint(MA, data(-1000), data(1000));
		Matrix<data> MB(row, column);
		populateMatrixWithRandomFixedPoint(MB, data(-1000), data(1000));
		Matrix<data> MC(row, column);
		
		//Perform Experiment on fixed point data type
		performMatrixMultiplication(MA, MB, MC, row, column, cacheMissOp, multiThread, simd, threadNum, native);
	}
	else if(data_type == "-4"){
		cout << "FLOATING POINT DATA" << endl;
		typedef float data;		//data becomes alias for float type
		
		//initialize matrices
		Matrix<data> MA(row, column);
		populateMatrixWithRandomFloat(MA);
		Matrix<data> MB(row, column);
		populateMatrixWithRandomFloat(MB);
		Matrix<data> MC(row, column);
		
		//Perform Experiment on floating point data type
		performMatrixMultiplication(MA, MB, MC, row, column, cacheMissOp, multiThread, simd, threadNum, native);

	}
	else{
		cerr << "Unrecognized data type. try '-2' or '-4'. Exiting..." << endl;
		return 0;
	}
}
