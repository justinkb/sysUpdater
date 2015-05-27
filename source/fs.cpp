/*
 *  sysUpdater is an update app for the Nintendo 3DS.
 *  Copyright (C) 2015 profi200
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/
 */


#include <algorithm>
#include <string>
#include <vector>
#include <ctime>
#include <3ds.h>
#include "error.h"
#include "fs.h"
#include "misc.h"

#define _FILE_ "fs.cpp" // Replacement for __FILE__ without the path



FS_archive sdmcArchive;


namespace fs
{
	//===============================================
	// class File                                  ||
	//===============================================

	// Default: read + write + create, sdmcArchive
	File::File(const std::u16string& path, u32 openFlags, FS_archive& archive)
	{
		open(path, openFlags, archive);
	}


	void File::open(const std::u16string& path, u32 openFlags, FS_archive& archive)
	{
		FS_path filePath = {PATH_WCHAR, (path.length()*2)+2, (const u8*)path.c_str()};
		Result  res;

		// Save args for when we want to move the file or other uses
		_path_      = path;
		_openFlags_ = openFlags;
		_archive_   = &archive;



		seek(0, FS_SEEK_SET); // Reset current offset
		close(); // Close file handle before we open a new one
		if((res = FSUSER_OpenFile(nullptr, &_fileHandle_, archive, filePath, openFlags, FS_ATTRIBUTE_NONE)))
			throw error(_FILE_, __LINE__, res);
	}


	u32 File::read(void *buf, u32 size)
	{
		u32 bytesRead;
		Result res;



		if((res = FSFILE_Read(_fileHandle_, &bytesRead, _offset_, buf, size)))
			throw error(_FILE_, __LINE__, res);

		_offset_ += bytesRead;
		return bytesRead;
	}


	u32 File::write(const void *buf, u32 size)
	{
		u32 bytesWritten;
		Result res;



		if((res = FSFILE_Write(_fileHandle_, &bytesWritten, _offset_, buf, size, FS_WRITE_FLUSH)))
			throw error(_FILE_, __LINE__, res);

		_offset_ += bytesWritten;
		return bytesWritten;
	}


	void File::seek(const u64 offset, fsSeekMode mode)
	{
		switch(mode)
		{
			case FS_SEEK_SET:
				_offset_ = offset;
				break;
			case FS_SEEK_CUR:
				_offset_ += offset;
				break;
			case FS_SEEK_END:
				_offset_ = size() - offset;
		}
	}


	u64 File::size()
	{
		u64 tmp;
		Result res;



		if((res = FSFILE_GetSize(_fileHandle_, &tmp))) throw error(_FILE_, __LINE__, res);

		return tmp;
	}


	void File::setSize(const u64 size)
	{
		Result res;



		if((res = FSFILE_SetSize(_fileHandle_, size))) throw error(_FILE_, __LINE__, res);
	}


	// This can also be used to rename files
	void File::move(const std::u16string& dst, FS_archive& dstArchive)
	{
		FS_path srcPath = {PATH_WCHAR, (_path_.length()*2)+2, (const u8*)_path_.c_str()};
		FS_path dstPath = {PATH_WCHAR, (dst.length()*2)+2, (const u8*)dst.c_str()};
		Result res;



		close(); // Close file handle before we open a new one
		if((res = FSUSER_RenameFile(nullptr, *_archive_, srcPath, dstArchive, dstPath))) throw error(_FILE_, __LINE__, res);
		open(dst, _openFlags_ & 3, dstArchive); // Open moved file
	}


	void File::copy(const std::u16string& dst, FS_archive& dstArchive)
	{
		File outFile(dst, FS_OPEN_WRITE | FS_OPEN_CREATE, dstArchive);
		u32 blockSize;
		u64 inFileSize, offset = 0;



		seek(0, FS_SEEK_SET);
		inFileSize = size();
		outFile.setSize(inFileSize);


		Buffer<u8> buffer(MAX_BUF_SIZE);


		for(u32 i=0; i<=inFileSize / MAX_BUF_SIZE; i++)
		{
			blockSize = ((inFileSize - offset<MAX_BUF_SIZE) ? inFileSize - offset : MAX_BUF_SIZE);

			if(blockSize>0)
			{
				read(&buffer, blockSize);
				outFile.write(&buffer, blockSize);

				offset += blockSize;
			}
		}
	}


	void File::remove(const std::u16string& path, FS_archive& archive)
	{
		FS_path lowPath = {PATH_WCHAR, (path.length()*2)+2, (const u8*)path.c_str()};
		Result res;



		if((res = FSUSER_DeleteFile(nullptr, archive, lowPath))) throw error(_FILE_, __LINE__, res);
	}


	void File::remove()
	{
		close(); // Close file handle before we can delete this file
		remove(_path_, *_archive_);
	}



	//===============================================
	// Directory related functions                 ||
	//===============================================

	void makeDir(const std::u16string& path, FS_archive& archive)
	{
		FS_path dirPath = {PATH_WCHAR, (path.length()*2)+2, (const u8*)path.c_str()};
		Result res;



		if((res = FSUSER_CreateDirectory(nullptr, archive, dirPath)) != FS_ERR_DOES_ALREADY_EXIST && res != 0)
			throw error(_FILE_, __LINE__, res);
	}


	void makePath(const std::u16string& path, FS_archive& archive)
	{
		std::u16string tmp;
		size_t found = 0;



		while(found != std::u16string::npos)
		{
			found = path.find_first_of(u"/", found+1);
			tmp.assign(path, 0, found);
			makeDir(tmp, archive);
		}
	}


	std::vector<DirEntry> listDirContents(const std::u16string& path, FS_archive& archive)
	{
		Handle dirHandle;
		Result res;
		u32 entriesRead;

		FS_path dirPath = {PATH_WCHAR, (path.length()*2)+2, (const u8*)path.c_str()};
		std::vector<DirEntry> filesFolders;
		DirEntry tmp;



		if((res = FSUSER_OpenDirectory(nullptr, &dirHandle, archive, dirPath))) throw error(_FILE_, __LINE__, res);


		Buffer<FS_dirent> entries(32);


		do
		{
			entriesRead = 0;
			filesFolders.reserve(filesFolders.size()+32); // Save time by reserving enough mem
			if((res = FSDIR_Read(dirHandle, &entriesRead, 32, &entries))) throw error(_FILE_, __LINE__, res);

			for(u32 i=0; i<entriesRead; i++)
			{
				tmp.name = (char16_t*)entries[i].name;
				tmp.isDir = entries[i].isDirectory;
				tmp.size = entries[i].fileSize;
				filesFolders.push_back(tmp);
			}
		} while(entriesRead == 32);



		// If we reserved too much mem shrink it
		filesFolders.shrink_to_fit();

		// Sort folders and files
		std::sort(filesFolders.begin(), filesFolders.end(), fileNameCmp);



		if((res = FSDIR_Close(dirHandle))) throw error(_FILE_, __LINE__, res);

		return filesFolders;
	}


	void copyDir(const std::u16string& src, const std::u16string& dst, FS_archive& srcArchive, FS_archive& dstArchive)
	{
		u32 depth = 0;
		u16 helper[64]; // Anyone uses higher dir depths?
		File f;

		std::u16string tmpInPath(src);
		std::u16string tmpOutPath(dst);



		// Create the specified path if it doesn't exist
		makePath(tmpOutPath, dstArchive);
		std::vector<DirEntry> entries = listDirContents(tmpInPath, srcArchive);
		helper[0] = 0; // We are in the root at file/folder 0


		while(1)
		{
			// Prevent non-existent member access
			if((helper[depth]>=entries.size()) ? 0 : entries[helper[depth]].isDir)
			{
				addToPath(tmpInPath, entries[helper[depth]].name);
				addToPath(tmpOutPath, entries[helper[depth]].name);
				makeDir(tmpOutPath, dstArchive);

				helper[++depth] = 0; // Go 1 up in the fs tree and reset position
				entries = listDirContents(tmpInPath, srcArchive);
			}

			if((helper[depth]>=entries.size()) ? 0 : entries[helper[depth]].isDir) continue;

			for(auto i : entries)
			{
				if(!i.isDir)
				{
					addToPath(tmpInPath, i.name);
					addToPath(tmpOutPath, i.name);
					f.open(tmpInPath, FS_OPEN_READ, srcArchive);
					f.copy(tmpOutPath, dstArchive);
					removeFromPath(tmpInPath);
					removeFromPath(tmpOutPath);
				}
			}

			if(!depth) break;

			removeFromPath(tmpInPath);
			removeFromPath(tmpOutPath);

			helper[--depth]++; // Go 1 down in the fs tree and increase position
			entries = listDirContents(tmpInPath, srcArchive);
		}
	}


	void removeDir(const std::u16string& path, FS_archive& archive)
	{
		FS_path dirPath = {PATH_WCHAR, (path.length()*2)+2, (const u8*)path.c_str()};
		Result res;



		if((res = FSUSER_DeleteDirectoryRecursively(nullptr, archive, dirPath))) throw error(_FILE_, __LINE__, res);
	}


	//===============================================
	// Zip related functions                       ||
	//===============================================

	// Removed. Will be available if it is ready.


	//===============================================
	// Misc functions                              ||
	//===============================================

	void addToPath(std::u16string& path, const std::u16string& dirOrFile)
	{
		if(path.length()>1) path.append(u"/" + dirOrFile);
		else path.append(dirOrFile);
	}


	void removeFromPath(std::u16string& path)
	{
		size_t lastSlash = path.find_last_of(u"/");

		if(lastSlash>1) path.erase(lastSlash);
		else path.erase(lastSlash+1);
	}
} // namespace fs


void sdmcArchiveInit()
{
	sdmcArchive = {ARCH_SDMC, {PATH_EMPTY, 0, nullptr}};
	FSUSER_OpenArchive(nullptr, &sdmcArchive);
}

void sdmcArchiveExit()
{
	FSUSER_CloseArchive(nullptr, &sdmcArchive);
}