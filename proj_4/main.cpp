#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cctype>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <random>
#include <immintrin.h>
#include "FastPFor/headers/variablebyte.h"	// For integer compression

// Global variables
std::unordered_map<std::string, int> globalDictionary;
std::vector<int> encoded_data;
std::mutex globalMtx;
std::string query;
std::string prefix;

// Function to read the column file
int readFile(std::string file, std::vector<std::string>& raw_data){
    std::ifstream input(file);
    if (input.is_open()) {
        std::string line;
        while (std::getline(input, line)) {
            // Remove special characters from the line
            line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char c) {
                return !std::isalnum(c) && !std::isspace(c); // Keep alphanumeric characters and spaces
            }), line.end());
            raw_data.push_back(line);
        }
        input.close();
    } else {
        std::cerr << "Failed to open file." << std::endl;
        return 0;
    }
    return 1;
}
void writeFiles(std::vector<uint32_t>& compressed_data,                     std::unordered_map<std::string, int> dictionary){
    // Output the compressed encoded data to a file
    std::ofstream encodedFile("encoded_data.txt");
    if (encodedFile.is_open()) {
        for (int code : compressed_data) {
            encodedFile << code << "\n";
        }
        encodedFile.close();
        std::cout << "Encoded data has been saved to 'encoded_data.txt'" << std::endl;
    } else {
        std::cerr << "Failed to create encoded data file." << std::endl;
    }
    
    // Output the dictionary to a file
    std::ofstream dictionaryFile("dictionary.txt");
    if (dictionaryFile.is_open()) {
        for (const auto& entry : dictionary) {
            dictionaryFile << entry.first << " : " << entry.second << "\n";
        }
        dictionaryFile.close();
        std::cout << "Dictionary has been saved to 'dictionary.txt'" << std::endl;
    } else {
        std::cerr << "Failed to create dictionary file." << std::endl;
    }
}
void Query_baseline(const std::vector<std::string>& raw_data, std::string item, std::vector<int>& indices){
    for (int i = 0; i < raw_data.size(); ++i) {
        if (raw_data[i] == item) {
            indices.push_back(i);
        }
    }
}
void Query_dict(std::string query, std::vector<int>& indices){
    int val;
    auto it = globalDictionary.find(query);
    if (it != globalDictionary.end()){
        val = it->second;
    }
    else {
        val = -1;
    }
    if(val != -1){
        for(int i = 0; i < encoded_data.size(); i++){
            if (encoded_data[i] == val){
                indices.push_back(i);
            }
        }
    }
}

void Query_SIMD(std::string query, std::vector<int>& indices) {
    int val; // Initialize a variable to hold the value associated with the query

    // Check if the query exists in the global dictionary
    auto it = globalDictionary.find(query);
    if (it != globalDictionary.end()) {
        val = it->second; // If found, retrieve its associated value
    } else {
        val = -1; // If not found, assign a sentinel value (-1) to 'val'
    }

    if (val != -1) { // Proceed only if the query value exists in the dictionary
        const int* dataPtr = encoded_data.data(); // Pointer to the encoded data
        const int step = 8; // Define the step size for SIMD operations (adjust according to SIMD register width)

        __m256i targetVec = _mm256_set1_epi32(val); // Create a SIMD vector with the query value

        // Iterate through the encoded data in steps of 'step' for SIMD processing
        for (int i = 0; i < encoded_data.size(); i += step) {
            __m256i dataVec = _mm256_loadu_si256((__m256i*)&dataPtr[i]); // Load a vector of integers from encoded data

            __m256i result = _mm256_cmpeq_epi32(dataVec, targetVec); // Compare the vectors for equality

            int mask = _mm256_movemask_ps(_mm256_castsi256_ps(result)); // Extract the match mask

            // Process the mask to identify matching indices within the SIMD vector
            for (int j = 0; j < step; ++j) {
                if ((mask >> j) & 1) { // Check each bit in the mask
                    indices.push_back(i + j); // Add the index where the match is found to the indices vector
                }
            }
        }
    }
}

void queryDataItem(const std::vector<std::string>& raw_data, std::string query, std::vector<int>& indices){
    
    // auto start = std::chrono::high_resolution_clock::now();
    // Query_baseline(raw_data, query, indices);
    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // std::cout << "Baseline took " << duration.count() << " microseconds." << std::endl;
    
    //---------------------------------------------------------------------------------
    
    // auto start = std::chrono::high_resolution_clock::now();
    // //Dict without simd
    // Query_dict(query, indices);
    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
   
    // std::cout << "Query with no SIMD took " << duration.count() << " microseconds." << std::endl;

    //---------------------------------------------------------------------------------
    
    auto start = std::chrono::high_resolution_clock::now();
    //Dict with simd
    Query_SIMD(query, indices);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Query with SIMD took " << duration.count() << " microseconds." << std::endl;
}

void prefix_baseline(const std::vector<std::string>& raw_data, std::string prefix, std::vector<int>& matchingPrefix){
    int ctr = 0;
    for (const auto& str : raw_data){
        if (str.compare(0, prefix.length(), prefix) == 0) {
            matchingPrefix.push_back(ctr);
        }
        ctr++;
    }
}
void prefix_dict(std::string prefix, std::vector<int>& matchingPrefix){
    
    std::vector<int> matchingValues;

    // Find matching values for the given prefix in the dictionary
    for (const auto& entry : globalDictionary) {
        if (entry.first.compare(0, prefix.length(), prefix) == 0) {
            matchingValues.push_back(entry.second);
        }
    }

    // Find indices of matching values in the vector of integers
    for (size_t i = 0; i < encoded_data.size(); ++i) {
        if (std::find(matchingValues.begin(), matchingValues.end(), encoded_data[i]) != matchingValues.end()) {
            matchingPrefix.push_back(i);
        }
    }
}
// Function to find indices of matching integer values using SIMD intrinsics for AVX2
void prefix_SIMD(std::string prefix, std::vector<int>& matchingPrefix){
    for (const auto& pair : globalDictionary) {
        if (pair.first.rfind(prefix, 0) == 0) {
            __m256i prefixInt = _mm256_set1_epi32(pair.second);

            for (size_t i = 0; i < encoded_data.size(); i += 8) {
                if (i + 8 <= encoded_data.size()) {
                    // Load 8 integers from the encoded data points vector
                    __m256i dataPoints = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&encoded_data[i]));

                    // Compare for equality with the value from the dictionary
                    __m256i matchResult = _mm256_cmpeq_epi32(dataPoints, prefixInt);

                    // Get a mask of matching elements
                    int matchMask = _mm256_movemask_ps(_mm256_castsi256_ps(matchResult));

                    // If there are matches, process the mask and update indices
                    while (matchMask != 0) {
                        int tz = __builtin_ctz(matchMask); // Count trailing zeros
                        matchingPrefix.push_back(i + tz);
                        matchMask &= ~(1 << tz);
                    }
                }
            }
        }
    }
}

void prefixSearch(const std::vector<std::string>& raw_data, std::string prefix, std::vector<int>& matchingPrefix){
    
    // auto start = std::chrono::high_resolution_clock::now();
    // prefix_baseline(raw_data, prefix, matchingPrefix);
    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // std::cout << "Prefix baseline took " << duration.count() << " microseconds." << std::endl;

    //---------------------------------------------------------------------------------
    
    // auto start = std::chrono::high_resolution_clock::now();
    // prefix_dict(prefix, matchingPrefix);
    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // std::cout << "Prefix with no SIMD took " << duration.count() << " microseconds." << std::endl;

    //---------------------------------------------------------------------------------
    
    auto start = std::chrono::high_resolution_clock::now();
    prefix_SIMD(prefix, matchingPrefix);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Prefix with SIMD took " << duration.count() << " microseconds." << std::endl;

}

// Function to get the encoded data column and dictionary
void encode_data(const std::vector<std::string>& raw_data, int start, int end, std::unordered_map<std::string, int>& localDict, std::vector<int>& localEncodedData) {
    int code = 0; // Initialize a code for new entries in the dictionary
    for (int i = start; i < end; ++i) {
        const std::string& str = raw_data[i];
        
        auto it = localDict.find(str); // Check if string already in dict.
        
        if (it == localDict.end()) { // If the string is not in the dict.
            localDict[str] = code;  // Add to dict.
            localEncodedData.push_back(code);
            code++;
        }
        else{
            localEncodedData.push_back(it->second); // If the string is found, use its corresponding code and add it to the encoded data
        }
    }
}
// Function to merge local encoded data created by threads
void mergeLocalEncodedData(const std::vector<std::vector<int>>& localEncodedDataList) {
    for (const auto& localEncodedData : localEncodedDataList) {
        encoded_data.insert(encoded_data.end(), localEncodedData.begin(), localEncodedData.end());
    }
}
// Function to merge local dictionaries created by threads
void mergeLocalDictionaries(const std::vector<std::unordered_map<std::string, int>>& localDicts) {
    std::lock_guard<std::mutex> lock(globalMtx);
    for (const auto& localDict : localDicts) {
        for (const auto& entry : localDict) {
            // Check if the string key already exists in the global dictionary
            if (globalDictionary.find(entry.first) == globalDictionary.end()) {
                globalDictionary[entry.first] = entry.second;  // If not present, add the entry
            }
        }
    }
}

// Function to manage the thread number and execute multi-threading operation
void callThreads(const std::vector<std::string>& raw_data){
    int num_threads;
    // Get the number of threads from the user with input validation
    while (true) {
        std::cout << "Enter the number of threads (1-" << std::thread::hardware_concurrency() << "): ";
        std::string input;
        std::getline(std::cin, input);

        std::istringstream iss(input);
        if (iss >> num_threads && num_threads >= 1 && num_threads <= std::thread::hardware_concurrency()) {
            break;
        } else {
            std::cout << "Invalid input. Please enter a valid number of threads." << std::endl;
        }
    } 

    std::vector<std::thread> threads;
    std::vector<std::unordered_map<std::string, int>> localDictionaries(num_threads); // Store local dictionary encodings
    std::vector<std::vector<int>> localEncodedDataList(num_threads); // Store local encoded data
    const int data_size = raw_data.size();
    const int segment_size = data_size / num_threads;

    //measure time to encode under variable # of threads
    auto start = std::chrono::high_resolution_clock::now();

    // This loop divides the work among multiple threads to encode data concurrently
    for (int i = 0; i < num_threads; ++i) {
        int start = i * segment_size; // starting index for current thread's segment
        int end = (i == num_threads - 1) ? data_size : (i + 1) * segment_size; // ending index
        
        // Create threads and assign the 'encode_data' function to each thread, passing references to necessary data
        threads.emplace_back(encode_data, std::ref(raw_data), start, end, std::ref(localDictionaries[i]), std::ref(localEncodedDataList[i]));
    }
    
    // Wait for all the threads to finish their work before continuing
    for (auto &t : threads) {
        t.join(); // Synchronize the main thread with each of the created threads
    }

    mergeLocalDictionaries(localDictionaries);
    mergeLocalEncodedData(localEncodedDataList);
    
    //Measure time duration and output
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "With " << num_threads << " threads, encoding took " << duration.count() << " microseconds." << std::endl;
}
// Function for compressing data
std::vector<uint32_t> compressData(const std::vector<int>& data) {
    // Use reinterpret_cast to work directly with the data without changing types
    const uint32_t* uintData = reinterpret_cast<const uint32_t*>(data.data());
    size_t dataLength = data.size();

    std::vector<uint32_t> compressed_data(data.size() * 2); // Estimate the size

    size_t nvalue = 0;
    FastPForLib::VariableByte varByte;
    varByte.encodeArray(uintData, dataLength, compressed_data.data(), nvalue);

    compressed_data.resize(nvalue);
    return compressed_data;
}

// Function for decompressing data
std::vector<int> decompressData(const std::vector<uint32_t>& compressed_data, size_t original_size) {
    std::vector<uint32_t> decompressed_data(original_size);

    size_t nvalue = 0;
    FastPForLib::VariableByte varByte;
    varByte.decodeArray(compressed_data.data(), compressed_data.size(), decompressed_data.data(), nvalue);

    decompressed_data.resize(nvalue);
    std::vector<int> restored_data(decompressed_data.begin(), decompressed_data.end());
    return restored_data;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    std::vector<std::string> raw_data;
    // Populate raw data vector
    if(!readFile(filename, raw_data)){
        std::cout << "Program Terminated" << std::endl;
        return 0;
    }


    // Perform dictionary encoding with multi-threading
    callThreads(raw_data);
    // Compress the data
    std::vector<uint32_t> compressed_data = compressData(encoded_data);

    //write files to SSD
    writeFiles(compressed_data, globalDictionary);

    // Decompress the data
    // std::vector<int> restored_data = decompressData(compressed_data, encoded_data.size());
    
    // Initialize random number generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, raw_data.size() - 1);

    // Get a random index
    int randomIndex = distrib(gen);

    // Use the random index to fetch a random string from the vector and a prefix from that string
    query = raw_data[randomIndex];
    prefix = query.substr(0, 3);
    std::cout << query << std::endl;
    std::cout << prefix << std::endl;
    
    std::vector<int> indices;
    queryDataItem(raw_data, query, indices);

    // For testing purposes, not recommended when dealing with large text files
    if (!indices.size()) {
        std::cout << "No matches found for '" << query << "'." << std::endl;
    } else {
        std::cout << "\nMatches found for " << query << "Indices: ";
        for (int index : indices) {
            std::cout << index << " ";
        }
        std::cout << std::endl;
    }
    //---//

    std::vector<int> matchingPrefix;
    prefixSearch(raw_data, prefix, matchingPrefix);
    
    //Also for testing purposes
    if (!matchingPrefix.size()) {
        std::cout << "No matches found for '" << prefix << "'." << std::endl;
    } else {
        std::cout << "Matches found for " << prefix << " Indices: ";
        for (int index : matchingPrefix) {
            std::cout << index << " ";
        }
        std::cout << std::endl;
    }
    //---//

    return 0;
}
