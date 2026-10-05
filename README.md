Implementation of ext2 filesystem driver in C++(Reading Binary Image Files):

Removed padding in struct to match the layout

Task 1: Made a superblock and group descriptor struct which contains metadata
        Read from the binary file and stored the metadata and outputted it

Task 2: Made an inode and directory entry struct (metadata)
        Found group and index of inode by using superblock and changing to 0 index
        Traversed directory by using upto second indirect pointer (file size ~ 12 mb)          Printed recursively from root folder(inode number = 2)

Task 3: Made function for getting file blocks and another function for displaying text         in terminal(needs inode number as input, can use output from task 2).
        For other type of files, made a function where file will be saved locally in           disk.
        In this case:
        1. inode 12 had a txt file so outputted in terminal
        2. inode 16 had pdf (thx for book)
        3. inode 15 had video scam >:(

Task 4: Made function for appending and overwriting, limited to 12 direct blocks.
        For appending, made an allocater function for allocating space.
        Overwriting function only overwrites the given bits, others remain intact.
