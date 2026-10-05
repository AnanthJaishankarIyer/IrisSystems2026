#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

// little-endian : method of storing multibyte data where the LSB (the "little end") is placed at the lowest memory address
// ext2 stores all multi-byte numerical fields and metadata in little-endian byte order

#pragma pack(push, 1) // removes extra pads in structure padding

struct Ext2Superblock
{
    uint32_t s_inodes_count;      // total number of inodes, 4 bytes
    uint32_t s_blocks_count;      // total number of blocks, 4 bytes
    uint32_t s_r_blocks_count;    // reserved blocks count, 4 bytes
    uint32_t s_free_blocks_count; // number of free blocks, 4 bytes
    uint32_t s_free_inodes_count; // number of free inodes, 4 bytes
    uint32_t s_first_data_block;  // either 0 or 1, 4 bytes
    uint32_t s_log_block_size;    // block size = 1024 << s_log_block_size, 4 bytes
    uint32_t s_log_frag_size;     // fragment size = 1024 << s_log_frag_size, 4 bytes
    uint32_t s_blocks_per_group;  // total blocks per group, 4 bytes
    uint32_t s_frags_per_group;   // total fragments per group, 4 bytes
    uint32_t s_inodes_per_group;  // total inodes per group, 4 bytes
    uint32_t s_mtime;             // mount time, 4 bytes
    uint32_t s_wtime;             // write time, 4 bytes
    uint16_t s_mnt_count;         // mount count, 2 bytes
    uint16_t s_max_mnt_count;     // max mount count, 2 bytes
    uint16_t s_magic;             // magic number, Value: 0xEF53 (stored as two bytes: 0x53 followed by 0xEF in little-endian order), used for identification of ext2 filesystem, 2 bytes
    uint16_t s_state;             // file system state, 1 -> clean, 2 -> error, 2 bytes
    uint16_t s_errors;            // behaviour when detecting errors (1->ignore, 2->remount as read only, 3-> kernel panic), 2 bytes
    uint16_t s_minor_rev_level;   // Minor revision level, 2 bytes
    uint32_t s_lastcheck;         // Time of last check, 4 bytes
    uint32_t s_checkinterval;     // Max. time between checks, 4 bytes
    uint32_t s_creator_os;        // Creator OS, 4 bytes
    uint32_t s_rev_level;         // Revision level (0 = original, 1 = dynamic), 4 bytes
    uint16_t s_def_resuid;        // Default uid for reserved blocks, 2 bytes
    uint16_t s_def_resgid;        // Default gid for reserved blocks, 2 bytes

    // sum of all bytes so far = 84

    // extended superblock, implemented only partially
    uint32_t s_first_ino;  // First non-reserved inode
    uint16_t s_inode_size; // Size of inode structure (usually 128)

    // remaining bytes up to 1023 are omitted for short code
    uint8_t s_reserved[934]; // 1024 - 90
};

// Ext2 Block Group Descriptor Structure (32 Bytes)
struct Ext2GroupDescriptor
{
    uint32_t bg_block_bitmap;      // block id of block allocation bitmap (0->free, 1->allocated)
    uint32_t bg_inode_bitmap;      // block id of inode allocation bitmap
    uint32_t bg_inode_table;       // block id of first inode table block
    uint16_t bg_free_blocks_count; // free blocks in group
    uint16_t bg_free_inodes_count; // free inodes in group
    uint16_t bg_used_dirs_count;   // directories in group
    uint16_t bg_pad;               // padding
    uint8_t bg_reserved[12];       // reserved space
};

#pragma pack(pop) // restores default padding settings

// Compile-time checks to protect struct alignments
static_assert(sizeof(Ext2Superblock) == 1024, "Ext2Superblock structure size must be exactly 1024 bytes!");
static_assert(sizeof(Ext2GroupDescriptor) == 32, "Ext2GroupDescriptor structure size must be exactly 32 bytes!");

#pragma pack(push, 1)

struct Ext2Inode
{
    uint16_t i_mode;        // File type and access rights
    uint16_t i_uid;         // Owner Uid
    uint32_t i_size;        // File size in bytes
    uint32_t i_atime;       // Access time
    uint32_t i_ctime;       // Creation time
    uint32_t i_mtime;       // Modification time
    uint32_t i_dtime;       // Deletion time
    uint16_t i_gid;         // Group Id
    uint16_t i_links_count; // Hard links count
    uint32_t i_blocks;      // 512-byte sectors allocated to data blocks
    uint32_t i_flags;       // File flags
    uint32_t i_osd1;        // OS specific data
    uint32_t i_block[15];   // Pointers to data blocks (12 direct, 3 indirect)
    uint32_t i_generation;  // File version
    uint32_t i_file_acl;    // File ACL
    uint32_t i_dir_acl;     // Directory ACL
    uint32_t i_faddr;       // Fragment address
    uint8_t i_osd2[12];     // OS specific data
};

struct Ext2DirEntry
{
    uint32_t inode;    // Inode number of file/folder
    uint16_t rec_len;  // Record length (offset padding to next entry)
    uint8_t name_len;  // Length of the string filename
    uint8_t file_type; // Type indicators (1=regular file, 2=directory)
    char name[255];    // Variable length name array
};

#pragma pack(pop)

class Ext2Driver
{
private:
    std::fstream disk; // reading and writing
    Ext2Superblock superBlock;
    std::vector<Ext2GroupDescriptor> groupTable;
    uint32_t blockSize;
    uint32_t groupCount;

public:
    bool mount(const std::string &imagePath)
    {
        // Open file in binary mode for both reading and writing
        disk.open(imagePath, std::ios::in | std::ios::out | std::ios::binary);
        if (!disk.is_open())
        {
            std::cerr << "Error: Failed to open disk image.\n";
            return false;
        }

        // Step 1: Read the Superblock at Offset 1024 (superblock begins at byte 1024 and goes til 2047)
        disk.seekg(1024, std::ios::beg);
        disk.read(reinterpret_cast<char *>(&superBlock), sizeof(Ext2Superblock));

        // Step 2: Validate magic number (0xEF53)
        if (superBlock.s_magic != 0xEF53)
        {
            std::cerr << "Error: Not a valid Ext2 file system.\n";
            return false;
        }

        // Step 3: Compute geometric layout
        blockSize = 1024 << superBlock.s_log_block_size;

        // Calculate total number of block groups (rounded up)
        groupCount = (superBlock.s_blocks_count + superBlock.s_blocks_per_group - 1) / superBlock.s_blocks_per_group;
        groupTable.resize(groupCount);

        // Step 4: Locate Group Descriptor Table
        // If block size is 1024, superblock is block 1, table is block 2 (offset 2048).
        // If block size is >1024, superblock is block 0, table is block 1 (offset = blockSize).
        uint64_t tableOffset = (blockSize == 1024) ? 2048 : blockSize;

        disk.seekg(tableOffset, std::ios::beg);
        disk.read(reinterpret_cast<char *>(groupTable.data()), groupCount * sizeof(Ext2GroupDescriptor));

        return true;
    }

    void displayMetadata()
    {
        std::cout << "=== EXT2 SUPERBLOCK INFO ===\n";
        std::cout << "Total Inodes   : " << superBlock.s_inodes_count << "\n";
        std::cout << "Total Blocks   : " << superBlock.s_blocks_count << "\n";
        std::cout << "Block Size     : " << blockSize << " bytes\n";
        std::cout << "Inodes/Group   : " << superBlock.s_inodes_per_group << "\n";
        std::cout << "Blocks/Group   : " << superBlock.s_blocks_per_group << "\n";
        std::cout << "Group Count    : " << groupCount << "\n\n";

        std::cout << "=== BLOCK GROUP DESCRIPTORS ===\n";
        for (uint32_t i = 0; i < groupCount; ++i)
        {
            std::cout << "Group " << i << " -> Inode Table Block: " << groupTable[i].bg_inode_table
                      << ", Free Blocks: " << groupTable[i].bg_free_blocks_count
                      << ", Free Inodes: " << groupTable[i].bg_free_inodes_count << "\n";
        }
    }

    Ext2Inode readInode(uint32_t inodeNum)
    {
        // Inodes are numbered starting at 1. Adjust offset:
        uint32_t adjustedIno = inodeNum - 1;

        // Find out which group holds this inode index
        uint32_t group = adjustedIno / superBlock.s_inodes_per_group;
        uint32_t index = adjustedIno % superBlock.s_inodes_per_group;

        // Fetch inode size from superblock (usually 128 bytes)
        uint32_t inodeSize = superBlock.s_inode_size;

        // Absolute physical position = (Inode Table Start Block * Block Size) + (Index * Inode Size)
        uint64_t offset = (static_cast<uint64_t>(groupTable[group].bg_inode_table) * blockSize) + (index * inodeSize);

        Ext2Inode inode;
        disk.seekg(offset, std::ios::beg);                              // go to offset
        disk.read(reinterpret_cast<char *>(&inode), sizeof(Ext2Inode)); // start reading 128 bytes from offset and store it in inode
        return inode;
    }

    void traverseDirectory(uint32_t inodeNum, const std::string &currentPath)
    {
        // Fetch the metadata structure for this specific directory's inode
        Ext2Inode inode = readInode(inodeNum);

        // Verify if this inode is actually flagged as a directory using the file type mask
        if ((inode.i_mode & 0xF000) != 0x4000) // if its 0x8000 its a file and if its 0xA000 its a symbolic link
            return;

        // Calculate how many total logical blocks this large directory spans based on its actual size
        uint32_t totalBlocksNeeded = (inode.i_size + blockSize - 1) / blockSize;

        // Determine how many 4-byte block pointers can fit inside a single data block
        const uint32_t pointersPerBlock = blockSize / 4;

        // Allocate a temporary memory buffer to read a single filesystem data block at a time
        std::vector<uint8_t> blockBuffer(blockSize);

        // Loop through every single logical block allocated to this directory
        for (uint32_t blockIndex = 0; blockIndex < totalBlocksNeeded; ++blockIndex)
        {
            uint32_t physicalBlock = 0;           // Stores the resolved block address on disk
            uint32_t logicalTracker = blockIndex; // Temp variable to track pointer offsets

            // LEVEL 1 RESOLUTION: Direct Blocks (Logical blocks 0 to 11)
            if (logicalTracker < 12)
            {
                physicalBlock = inode.i_block[logicalTracker];
            }
            else
            {
                logicalTracker -= 12; // Shift offset relative to the indirect pointers

                // LEVEL 2 RESOLUTION: Singly Indirect Block (i_block[12])
                if (logicalTracker < pointersPerBlock)
                {
                    uint32_t indirectBlock = inode.i_block[12];
                    if (indirectBlock != 0)
                    {
                        uint64_t byteOffset = (static_cast<uint64_t>(indirectBlock) * blockSize) + (logicalTracker * 4);
                        disk.seekg(byteOffset, std::ios::beg);
                        disk.read(reinterpret_cast<char *>(&physicalBlock), 4);
                    }
                }
                else
                {
                    logicalTracker -= pointersPerBlock; // Shift offset relative to the doubly indirect table

                    // LEVEL 3 RESOLUTION: Doubly Indirect Block (i_block[13])
                    if (logicalTracker < pointersPerBlock * pointersPerBlock)
                    {
                        uint32_t doublyIndirectBlock = inode.i_block[13];
                        if (doublyIndirectBlock != 0)
                        {
                            // Calculate primary index in first table and secondary index in second table
                            uint32_t primaryIndex = logicalTracker / pointersPerBlock;
                            uint32_t secondaryIndex = logicalTracker % pointersPerBlock;

                            // Read the mid-level pointer from the primary indirect block table
                            uint32_t midLevelBlock = 0;
                            uint64_t primaryOffset = (static_cast<uint64_t>(doublyIndirectBlock) * blockSize) + (primaryIndex * 4);
                            disk.seekg(primaryOffset, std::ios::beg);
                            disk.read(reinterpret_cast<char *>(&midLevelBlock), 4);

                            // Read the final physical block pointer from the secondary indirect block table
                            if (midLevelBlock != 0)
                            {
                                uint64_t secondaryOffset = (static_cast<uint64_t>(midLevelBlock) * blockSize) + (secondaryIndex * 4);
                                disk.seekg(secondaryOffset, std::ios::beg);
                                disk.read(reinterpret_cast<char *>(&physicalBlock), 4);
                            }
                        }
                    }
                }
            }

            // triply indirect block is not needed as file size here is 12mb

            // If the block maps to 0, it means it's an unallocated empty sparse block; skip it
            if (physicalBlock == 0)
                continue;

            // Position the disk file stream pointer to the absolute start of this physical block
            disk.seekg(static_cast<uint64_t>(physicalBlock) * blockSize, std::ios::beg);
            // Load the full block size worth of binary data into our buffer memory
            disk.read(reinterpret_cast<char *>(blockBuffer.data()), blockSize);

            // Reset our pointer tracker to read records starting from byte 0 of this specific block
            uint32_t currentOffset = 0;

            // Process directory entries packed inside this block until we reach the block ceiling
            while (currentOffset < blockSize)
            {
                // Cast the raw memory address at the current offset to a Directory Entry structure pointer
                Ext2DirEntry *entry = reinterpret_cast<Ext2DirEntry *>(&blockBuffer[currentOffset]);

                // Safety boundary check: records must have a non-zero length step to avoid loop hangs
                if (entry->rec_len == 0)
                    break;

                // If the entry points to inode 0, it indicates a deleted file entry; skip parsing it
                if (entry->inode != 0)
                {
                    // Reconstruct the file name from explicit length since it is not null-terminated
                    std::string name(entry->name, entry->name_len);

                    // Filter out standard relative anchors "." and ".." to block infinite loop cycles
                    if (name != "." && name != "..")
                    {
                        // Synthesize the absolute destination path string
                        std::string fullPath = currentPath + "/" + name;

                        // Output the discovered entity's tracking path details to console output
                        std::cout << fullPath << " (Inode: " << entry->inode << ", Type: "
                                  << (entry->file_type == 2 ? "DIR" : "FILE") << ")\n";

                        // If the entry record is explicitly flagged as a child directory, dive deeper
                        if (entry->file_type == 2)
                        {
                            // Recursively pass control to process the discovered child folder structure
                            traverseDirectory(entry->inode, fullPath);
                        }
                    }
                }
                // Advance the tracking index offset forward by the record's explicit length
                currentOffset += entry->rec_len;
            }
        }
    }

    void printTree()
    {
        std::cout << "=== RECURSIVE FILESYSTEM TREE ===\n";
        traverseDirectory(2, ""); // root directory usually starts at Inode 2
    }

    std::vector<uint32_t> getFileBlocks(const Ext2Inode &inode)
    {
        std::vector<uint32_t> blockList;
        uint32_t pointersPerBlock = blockSize / 4; // Each 32-bit block address takes 4 bytes

        // Collect from Direct Pointers
        for (int i = 0; i < 12; ++i)
        {
            if (inode.i_block[i] != 0) // 0 means empty
            {
                blockList.push_back(inode.i_block[i]);
            }
        }

        // Parse Singly Indirect Pointer, file exceeds 48kb
        if (inode.i_block[12] != 0)
        {
            std::vector<uint32_t> singleIndirect(pointersPerBlock);
            disk.seekg(static_cast<uint64_t>(inode.i_block[12]) * blockSize, std::ios::beg);
            disk.read(reinterpret_cast<char *>(singleIndirect.data()), blockSize);

            for (uint32_t b : singleIndirect)
            {
                if (b != 0) // checks for direct blocks
                    blockList.push_back(b);
            }
        }

        // Parse Doubly Indirect Pointer, file exceeds 4mb
        if (inode.i_block[13] != 0)
        {
            std::vector<uint32_t> doubleIndirect(pointersPerBlock);
            disk.seekg(static_cast<uint64_t>(inode.i_block[13]) * blockSize, std::ios::beg);
            disk.read(reinterpret_cast<char *>(doubleIndirect.data()), blockSize);

            for (uint32_t singleIndirectBlock : doubleIndirect)
            {
                if (singleIndirectBlock != 0) // checks for singly indirect blocks
                {
                    std::vector<uint32_t> singleIndirect(pointersPerBlock);
                    disk.seekg(static_cast<uint64_t>(singleIndirectBlock) * blockSize, std::ios::beg);
                    disk.read(reinterpret_cast<char *>(singleIndirect.data()), blockSize);

                    for (uint32_t b : singleIndirect)
                    {
                        if (b != 0) // checks for direct blocks
                            blockList.push_back(b);
                    }
                }
            }
        }
        return blockList; // returns list of block ids
    }

    void displayFileContent(uint32_t inodeNum)
    {
        Ext2Inode inode = readInode(inodeNum);
        std::vector<uint32_t> blocks = getFileBlocks(inode);

        uint32_t bytesRemaining = inode.i_size;
        std::vector<char> buffer(blockSize);

        std::cout << "=== FILE CONTENT FOR INODE " << inodeNum << " ===\n";
        for (uint32_t blockId : blocks)
        {
            if (bytesRemaining == 0) // block is empty
                break;

            disk.seekg(static_cast<uint64_t>(blockId) * blockSize, std::ios::beg);
            disk.read(buffer.data(), blockSize);

            uint32_t bytesToPrint = std::min(blockSize, bytesRemaining); // block size is 1024 if full
            std::cout.write(buffer.data(), bytesToPrint);
            bytesRemaining -= bytesToPrint;
        }
        std::cout << "\n==================================\n";
    }

    void saveFileToDisk(uint32_t inodeNum, const std::string &outputFilename)
    {
        Ext2Inode inode = readInode(inodeNum);
        std::vector<uint32_t> blocks = getFileBlocks(inode);
        std::ofstream outFile(outputFilename, std::ios::binary);
        if (!outFile)
        {
            std::cerr << "Error: Could not create output file: " << outputFilename << "\n";
            return;
        }
        uint32_t bytesRemaining = inode.i_size;
        std::vector<char> buffer(blockSize);
        std::cout << "Extracting Inode " << inodeNum << " into " << outputFilename << "...\n";
        for (uint32_t blockId : blocks)
        {
            if (bytesRemaining == 0)
                break;
            disk.seekg(static_cast<uint64_t>(blockId) * blockSize, std::ios::beg);
            disk.read(buffer.data(), blockSize);
            uint32_t bytesToWrite = std::min(blockSize, bytesRemaining);
            outFile.write(buffer.data(), bytesToWrite);
            bytesRemaining -= bytesToWrite;
        }
        std::cout << "Successfully saved! Total size: " << inode.i_size << " bytes.\n\n";
    }

    uint32_t allocateBlock(uint32_t preferredGroup = 0)
    {
        std::vector<uint8_t> bitmap(blockSize);

        for (uint32_t g = preferredGroup; g < groupCount; ++g)
        {
            uint64_t bitmapOffset = static_cast<uint64_t>(groupTable[g].bg_block_bitmap) * blockSize;
            disk.seekg(bitmapOffset, std::ios::beg);
            disk.read(reinterpret_cast<char *>(bitmap.data()), blockSize);

            for (uint32_t byteIdx = 0; byteIdx < blockSize; ++byteIdx)
            {
                if (bitmap[byteIdx] != 0xFF)
                { // At least one bit here is free (0)
                    for (int bitIdx = 0; bitIdx < 8; ++bitIdx)
                    {
                        if ((bitmap[byteIdx] & (1 << bitIdx)) == 0)
                        {
                            // Mark bit as allocated (1)
                            bitmap[byteIdx] |= (1 << bitIdx);

                            // Commit updated allocation bitmap back to physical disk storage
                            disk.seekp(bitmapOffset, std::ios::beg);
                            disk.write(reinterpret_cast<char *>(bitmap.data()), blockSize);

                            // Update free tracking stats inside structural metrics tables
                            groupTable[g].bg_free_blocks_count--;
                            superBlock.s_free_blocks_count--;

                            // Recalculate global absolute block id
                            uint32_t allocatedBlockId = (g * superBlock.s_blocks_per_group) + (byteIdx * 8) + bitIdx + superBlock.s_first_data_block;
                            return allocatedBlockId;
                        }
                    }
                }
            }
        }
        throw std::runtime_error("Disk Full Error: No blocks available.");
    }

    void appendToFile(uint32_t inodeNum, const std::string &newData)
    {
        Ext2Inode inode = readInode(inodeNum);
        std::vector<uint32_t> blocks = getFileBlocks(inode);

        uint32_t currentSize = inode.i_size;
        uint32_t bytesWritten = 0;
        uint32_t totalNewBytes = newData.length();

        // Check if we can write inside the tail end of the last allocated data block
        uint32_t tailOffset = currentSize % blockSize;
        if (tailOffset != 0 && !blocks.empty())
        {
            uint32_t lastBlock = blocks.back();
            uint32_t availableInTail = blockSize - tailOffset;
            uint32_t toWrite = std::min(availableInTail, totalNewBytes);

            disk.seekp((static_cast<uint64_t>(lastBlock) * blockSize) + tailOffset, std::ios::beg);
            disk.write(newData.c_str(), toWrite);

            bytesWritten += toWrite;
        }

        // If more blocks are needed, dynamically allocate space
        while (bytesWritten < totalNewBytes)
        {
            uint32_t newBlock = allocateBlock();

            // Find an open space slot inside the Direct Pointers matrix array
            int directSlot = -1;
            for (int i = 0; i < 12; ++i)
            {
                if (inode.i_block[i] == 0)
                {
                    directSlot = i;
                    break;
                }
            }

            if (directSlot != -1)
            {
                inode.i_block[directSlot] = newBlock;
            }
            else
            {
                // Out of simple direct slots. For real production designs, map indirection blocks (12, 13, 14).
                throw std::runtime_error("Complexity limit: This educational example supports up to 12 direct blocks.");
            }

            uint32_t toWrite = std::min(blockSize, totalNewBytes - bytesWritten);
            disk.seekp(static_cast<uint64_t>(newBlock) * blockSize, std::ios::beg);
            disk.write(newData.c_str() + bytesWritten, toWrite);

            bytesWritten += toWrite;
        }

        // Update Inode Metadata stats
        inode.i_size += totalNewBytes;
        inode.i_mtime = 1791000000; // Simulated timestamp placeholder

        // Commit changed file configuration variables back down to structural storage lines
        uint32_t adjustedIno = inodeNum - 1;
        uint32_t group = adjustedIno / superBlock.s_inodes_per_group;
        uint32_t index = adjustedIno % superBlock.s_inodes_per_group;
        uint64_t inodeOffset = (static_cast<uint64_t>(groupTable[group].bg_inode_table) * blockSize) + (index * superBlock.s_inode_size);

        disk.seekp(inodeOffset, std::ios::beg);
        disk.write(reinterpret_cast<char *>(&inode), sizeof(Ext2Inode));

        // Flush modifications globally back to the main filesystem layout
        disk.seekp(1024, std::ios::beg);
        disk.write(reinterpret_cast<char *>(&superBlock), sizeof(Ext2Superblock));
    }

    void overwriteFile(uint32_t inodeNum, uint32_t writeOffset, const std::string &newData)
    {
        Ext2Inode inode = readInode(inodeNum);
        uint32_t totalNewBytes = newData.length();

        if (totalNewBytes == 0)
            return;

        std::cout << "Overwriting Inode " << inodeNum << " at byte offset " << writeOffset
                  << " with " << totalNewBytes << " bytes of data...\n";

        uint32_t bytesWritten = 0;

        // Loop until all data bytes are committed to the filesystem
        while (bytesWritten < totalNewBytes)
        {
            uint32_t targetBytePos = writeOffset + bytesWritten;
            uint32_t logicalBlockIndex = targetBytePos / blockSize;
            uint32_t offsetWithinBlock = targetBytePos % blockSize;

            // Fetch current list of blocks to see if this logical block index already exists
            std::vector<uint32_t> currentBlocks = getFileBlocks(inode);
            uint32_t physicalBlock = 0;

            if (logicalBlockIndex < currentBlocks.size())
            {
                // Block already exists, modify it in place
                physicalBlock = currentBlocks[logicalBlockIndex];
            }
            else if (logicalBlockIndex < 12)
            {
                // Block does not exist yet but falls within Direct pointers limits; allocate one
                physicalBlock = allocateBlock();
                inode.i_block[logicalBlockIndex] = physicalBlock;
                std::cout << " Allocated new physical block " << physicalBlock
                          << " for logical block " << logicalBlockIndex << "\n";
            }
            else
            {
                // Out of simple direct slots
                throw std::runtime_error("Complexity limit: This educational example supports writing up to 12 direct blocks (12KB).");
            }

            // Calculate how much we can write in this block iteration
            uint32_t availableInBlock = blockSize - offsetWithinBlock;
            uint32_t toWrite = std::min(availableInBlock, totalNewBytes - bytesWritten);

            // Commit the raw character data to the physical block sector
            uint64_t diskOffset = (static_cast<uint64_t>(physicalBlock) * blockSize) + offsetWithinBlock;
            disk.seekp(diskOffset, std::ios::beg);
            disk.write(newData.c_str() + bytesWritten, toWrite);

            bytesWritten += toWrite;
        }

        // Update i_size ONLY if the overwrite extended past the original file boundary limit
        uint32_t potentialNewSize = writeOffset + totalNewBytes;
        if (potentialNewSize > inode.i_size)
        {
            inode.i_size = potentialNewSize;
        }

        inode.i_mtime = 1792000000; // Simulated timestamp placeholder

        // Commit updated inode structure changes back down to the virtual disk table
        uint32_t adjustedIno = inodeNum - 1;
        uint32_t group = adjustedIno / superBlock.s_inodes_per_group;
        uint32_t index = adjustedIno % superBlock.s_inodes_per_group;
        uint64_t inodeOffset = (static_cast<uint64_t>(groupTable[group].bg_inode_table) * blockSize) + (index * superBlock.s_inode_size);

        disk.seekp(inodeOffset, std::ios::beg);
        disk.write(reinterpret_cast<char *>(&inode), sizeof(Ext2Inode));

        // Flush modifications globally back to the main filesystem superblock layout
        disk.seekp(1024, std::ios::beg);
        disk.write(reinterpret_cast<char *>(&superBlock), sizeof(Ext2Superblock));

        std::cout << "Overwrite successfully committed!\n\n";
    }
};

int main()
{
    // Instantiate our standard, single-threaded Ext2 Filesystem Driver
    Ext2Driver filesystem;

    // Mount the raw virtual disk image file path
    std::cout << "Mounting disk image file...\n";
    if (!filesystem.mount("disk1.img"))
    {
        std::cerr << "Initialization failed. Exiting.\n";
        return -1;
    }

    // ==========================================
    // TASK 1: Display Global Volume Geometry
    // ==========================================
    filesystem.displayMetadata();

    std::cout << "\n\n\n";

    // TASK 2: Traverse Directory Layout Tree
    // ==========================================
    filesystem.printTree();

    std::cout << "\n\n\n";

    // TASK 3: Read File Data Blocks (Indirection Matrix Parsing)
    // On traversing directories(task 2) we find inode 12 matches with a txt file, so lets use that
    // for pdf file have to do lot of formatting
    // ==========================================
    uint32_t targetInode = 12;
    filesystem.displayFileContent(targetInode);

    // this function downloads the file in local folder instead of outputting in terminal(for non text files)

    // works for txt files too, its not duplicated if you run multiple times, will get duplicated if you save it under a different file name
    filesystem.saveFileToDisk(12, "secondfile.txt");

    // goated rickroll
    filesystem.saveFileToDisk(15, "scam_video.webm");

    // thx for the book
    filesystem.saveFileToDisk(16, "data_structures_book.pdf");

    std::cout << "\n\n\n";

    // TASK 4: Update/Append New Content to Existing Files
    // ==========================================
    std::cout << "\nAppending new record logs to Inode " << targetInode << "...\n";

    // This call modifies the bitmap, updates i_size, and appends raw data bytes
    filesystem.appendToFile(targetInode, "Nice idea");
    // cant revert back, appends multiple times if you call it

    // Re-verify the changes by printing the file contents once more
    std::cout << "\nReading updated file block structure again:\n";
    filesystem.displayFileContent(targetInode);

    //overwrites the file after given byte offset, bytes before and after remain intact
    filesystem.overwriteFile(targetInode, 12, "fourth");

    std::cout << "Reading block structure after middle offset 12 overwrite:\n";
    filesystem.displayFileContent(targetInode);

    return 0;
}