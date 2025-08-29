#include <iostream>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

// File comparison structure
struct FileInfo {
    fs::path path;
    uintmax_t size;
    std::string hash;
    
    FileInfo(fs::path p, uintmax_t s, std::string h) 
        : path(p), size(s), hash(h) {}
};

// Generate file hash (simple version without external dependencies)
std::string generate_file_hash(const fs::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open file: " + file_path.string());

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Simple hash combining size and content
    return std::to_string(content.size()) + "_" + std::to_string(std::hash<std::string>{}(content));
}

// Find all files in directory recursively
std::vector<FileInfo> find_files(const fs::path& directory) {
    std::vector<FileInfo> files;
    
    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                try {
                    uintmax_t size = entry.file_size();
                    files.emplace_back(entry.path(), size, "");
                }
                catch (...) {
                    std::cerr << "Error accessing: " << entry.path() << "\n";
                }
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << "\n";
    }
    
    return files;
}

// Find duplicate files
std::unordered_map<std::string, std::vector<FileInfo>> find_duplicates(
    const std::vector<FileInfo>& files) {
    
    std::unordered_map<std::string, std::vector<FileInfo>> duplicates;
    std::unordered_map<uintmax_t, std::vector<FileInfo>> size_groups;

    // Group by size first for optimization
    for (const auto& file : files) {
        size_groups[file.size].push_back(file);
    }

    // Generate hashes for potential duplicates
    for (const auto& [size, group] : size_groups) {
        if (group.size() > 1) {
            std::unordered_map<std::string, std::vector<FileInfo>> hash_groups;
            
            for (const auto& file : group) {
                try {
                    std::string hash = generate_file_hash(file.path);
                    hash_groups[hash].emplace_back(file.path, size, hash);
                }
                catch (const std::exception& e) {
                    std::cerr << "Error processing " << file.path << ": " << e.what() << "\n";
                }
            }

            for (const auto& [hash, files] : hash_groups) {
                if (files.size() > 1) {
                    duplicates[hash] = files;
                }
            }
        }
    }
    
    return duplicates;
}

// Handle duplicate files based on user choice
void handle_duplicates(
    const std::unordered_map<std::string, std::vector<FileInfo>>& duplicates,
    const std::string& action) {
    
    for (const auto& [hash, files] : duplicates) {
        std::cout << "\nDuplicate group (" << files.size() << " files)\n";
        std::cout << "Hash: " << hash << "\n";
        
        // Sort files by modification time (oldest first)
        std::vector<fs::path> sorted_files;
        for (const auto& file : files) {
            sorted_files.push_back(file.path);
        }
        std::sort(sorted_files.begin(), sorted_files.end(),
            [](const fs::path& a, const fs::path& b) {
                return fs::last_write_time(a) < fs::last_write_time(b);
            });

        // Keep the original (oldest file)
        const fs::path& original = sorted_files.front();
        std::cout << "Original: " << original << "\n";

        // Process duplicates
        for (auto it = sorted_files.begin() + 1; it != sorted_files.end(); ++it) {
            std::cout << "Duplicate: " << *it << "\n";
            
            if (action == "--delete") {
                try {
                    fs::remove(*it);
                    std::cout << "Deleted: " << *it << "\n";
                }
                catch (const fs::filesystem_error& e) {
                    std::cerr << "Delete failed: " << e.what() << "\n";
                }
            }
            else if (action == "--hardlink") {
                try {
                    fs::remove(*it);
                    fs::create_hard_link(original, *it);
                    std::cout << "Created hardlink: " << *it << "\n";
                }
                catch (const fs::filesystem_error& e) {
                    std::cerr << "Hardlink failed: " << e.what() << "\n";
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "File Deduplicator\n"
                  << "Usage: " << argv[0] << " <directory> [action]\n"
                  << "Actions:\n"
                  << "  --list      List duplicates only (default)\n"
                  << "  --delete    Delete duplicates\n"
                  << "  --hardlink  Replace duplicates with hardlinks\n";
        return 1;
    }

    const fs::path directory = argv[1];
    const std::string action = (argc >= 3) ? argv[2] : "--list";

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        std::cerr << "Invalid directory: " << directory << "\n";
        return 1;
    }

    try {
        std::cout << "Scanning directory: " << directory << "\n";
        auto files = find_files(directory);
        std::cout << "Found " << files.size() << " files\n";
        
        std::cout << "Looking for duplicates...\n";
        auto duplicates = find_duplicates(files);
        
        if (duplicates.empty()) {
            std::cout << "\nNo duplicates found!\n";
            return 0;
        }

        std::cout << "\nFound " << duplicates.size() << " groups of duplicates\n";
        handle_duplicates(duplicates, action);

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
