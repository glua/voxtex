#include <iostream>
#include <fstream>
#include <sstream>

#include <map>

#include "tinydir.h"

#include "vtflib.h"
#include "lodepng.h"

using namespace std;
using namespace VTFLib;

class PackerImage {
public:
	PackerImage();

	void init(int w, int h);
	bool loadFile(const char* filename);

	bool copyRegion(PackerImage& source, int x, int y, int w, int h);
	bool pasteRegion(PackerImage& source, int x, int y, double scale=1);

	bool savePNG(const char* filename);
	bool writeTextureData(CVTFFile& vtfFile, int miplevel = 0);

	vlUInt getWidth() { return width; }
	vlUInt getHeight() { return height; }
private:
	vector<vlByte> data;
	vlUInt width = 0;
	vlUInt height = 0;

	PackerImage(const PackerImage&) = delete;
	PackerImage& operator=(const PackerImage&) = delete;
};

PackerImage::PackerImage() {}

void PackerImage::init(int w, int h) {
	data.resize(w*h * 4);
	for (int i = 0; i < w*h * 4; i++) {
		data[i] = 0;
	}
	width = w;
	height = h;
}

bool PackerImage::loadFile(const char* filename) {

	const char* extension = filename + strlen(filename) - 4; // look at this shitcode

	if (strcmp(extension, ".png") == 0) {
		return !lodepng::decode(data, width, height, filename);
	}
	else if (strcmp(extension, ".vtf") == 0) {
		CVTFFile f;
		if (!f.Load(filename))
			return false;
		
		init(f.GetWidth(), f.GetHeight());
		
		return CVTFFile::ConvertToRGBA8888(f.GetData(0, 0, 0, 0), data.data(), width, height, f.GetFormat());
	}

	return false;
}

bool PackerImage::copyRegion(PackerImage& source, int x, int y, int w, int h) {
	if (x<0 || y<0 || x + w>source.width || y + h>source.height)
		return false;
	data.resize(w*h * 4);
	width = w;
	height = h;
	
	for (int px = 0; px < w; px++) {
		for (int py = 0; py < h; py++) {
			int source_base = (x + px + source.width*(y + py)) * 4;
			int dest_base = (px + width*py) * 4;

			data[dest_base] = source.data[source_base];
			data[dest_base + 1] = source.data[source_base + 1];
			data[dest_base + 2] = source.data[source_base + 2];
			data[dest_base + 3] = source.data[source_base + 3];
		}
	}
	return true;
}

bool PackerImage::pasteRegion(PackerImage& source, int x, int y, double scale) {
	PackerImage* real_source = &source;

	if (scale != 1) {
		real_source = new PackerImage();
		real_source->init(source.width*scale, source.height*scale);
		CVTFFile::Resize(source.data.data(), real_source->data.data(), source.width, source.height, real_source->width, real_source->height);
	}

	for (int px = 0; px < real_source->width; px++) {
		for (int py = 0; py < real_source->height; py++) {
			if (x + px < 0 || x + px >= width || y + py < 0 || y + py >= height)
				continue;

			int source_base = (px + real_source->width*py) * 4;
			int dest_base = (x + px + width*(y + py)) * 4;

			data[dest_base] = real_source->data[source_base];
			data[dest_base + 1] = real_source->data[source_base + 1];
			data[dest_base + 2] = real_source->data[source_base + 2];
			data[dest_base + 3] = real_source->data[source_base + 3];
		}
	}

	if (real_source != &source) {
		delete real_source;
	}

	return true;
}


bool PackerImage::savePNG(const char* filename) {
	return !lodepng::encode(filename, data, width, height);
}

bool PackerImage::writeTextureData(CVTFFile& vtfFile, int miplevel) {
	vlByte* dxt_formatted = new vlByte[width*height];
	if (!CVTFFile::ConvertFromRGBA8888(data.data(), dxt_formatted, width, height, IMAGE_FORMAT_DXT5)) {
		delete[] dxt_formatted;
		return false;
	}
	vtfFile.SetData(0, 0, 0, miplevel, dxt_formatted);
	delete[] dxt_formatted;
	return true;
}

bool dirIsValid(const char* dir_name) {
	tinydir_dir dir;

	if (tinydir_open(&dir, dir_name) == -1)
		return false;

	tinydir_close(&dir);

	return true;
}

bool taskSlice(const char* file, const char* outdir, int slice_width, int slice_height) {
	PackerImage img;
	
	if (!dirIsValid(outdir)) {
		cout << "[Slice] Output directory does not exist." << endl;
		return false;
	}
	
	if (!img.loadFile(file)) {
		cout << "[Slice] Failed to load input file." << endl;
		return false;
	}
	int out_width = img.getWidth() / slice_width;
	int out_height = img.getHeight() / slice_height;
	cout << "[Slice] Output dimensions: " << out_width << " x " << out_height << endl;
	if (out_width != out_height) {
		cout << "[Slice] Output dimensions must be square!" << endl;
		return false;
	}

	int output_count = slice_width*slice_height;
	for (int i = 0; i < output_count; i++) {
		int ix = i % slice_width;
		int iy = i / slice_height;
		PackerImage slice;
		slice.copyRegion(img, ix*out_width, iy*out_height, out_width, out_height);

		stringstream outfile;
		outfile << outdir << "/" << i << ".png";
		outfile.str().c_str();

		if (!slice.savePNG(outfile.str().c_str())) {
			cout << "[Slice] Failed to save image " << i + 1 << " / " << output_count << " : " << outfile.str() << endl;
			return false;
		}
	}
	cout << "[Slice] Success! Saved all " << output_count << " images." << endl;
	return true;
}

//weee http://stackoverflow.com/questions/1322510/given-an-integer-how-do-i-find-the-next-largest-power-of-two-using-bit-twiddlin
int nextPow2(int n) {
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;
	return n;
}

bool taskPack(const char* src_dir,const char* out_file,bool simple,bool pad) {

	if (!dirIsValid(src_dir)) {
		cout << "[Pack] Source directory does not exist!" << endl;
		return false;
	}

	tinydir_dir dir;
	tinydir_open(&dir, src_dir);

	int count = -1;

	int img_size = 0;

	map<int, PackerImage> images;

	while (dir.has_next) {
		tinydir_file file;
		tinydir_readfile(&dir, &file);

		if (strcmp(file.extension, "png") == 0) {
			int n;
			stringstream s;
			s << file.name;
			s >> n;
			if (s.fail()) {
				cout << "[Pack] Could not read index from source file: " << file.name << endl;
				return false;
			}

			count = max(n, count);

			int iw, ih;

			if (pad) {
				PackerImage tmp;

				if (!tmp.loadFile(file.path)) {
					cout << "[Pack] Failed to load file: " << file.name << endl;
					return false;
				}

				iw = tmp.getWidth();
				ih = tmp.getHeight();

				images[n].init(iw*2,ih*2);

				images[n].pasteRegion(tmp, -iw / 2, -ih / 2);
				images[n].pasteRegion(tmp, iw / 2, -ih / 2);
				images[n].pasteRegion(tmp, iw / 2 + iw, -ih / 2);

				images[n].pasteRegion(tmp, -iw / 2, ih / 2);
				images[n].pasteRegion(tmp, iw / 2, ih / 2);
				images[n].pasteRegion(tmp, iw / 2 + iw, ih / 2);

				images[n].pasteRegion(tmp, -iw / 2, ih / 2 + ih);
				images[n].pasteRegion(tmp, iw / 2, ih / 2 + ih);
				images[n].pasteRegion(tmp, iw / 2 + iw, ih / 2 + ih);
			}
			else {
				if (!images[n].loadFile(file.path)) {
					cout << "[Pack] Failed to load file: " << file.name << endl;
					return false;
				}

				iw = images[n].getWidth();
				ih = images[n].getHeight();
			}

			if (iw != ih) {
				cout << "[Pack] Source image not square: " << file.name << " ( " << iw << " x " << ih << " )" << endl;
				return false;
			}

			if (img_size == 0) {
				img_size = iw;
				cout << "[Pack] Assuming dimensions will match file: " << file.name << " ( " << iw << " x " << ih << " )" << endl;
			}
			else if (img_size!=iw) {
				cout << "[Pack] Source image dimensions do not match: " << file.name << " ( " << iw << " x " << ih << " )" << endl;
				return false;
			}
		}

		tinydir_next(&dir);
	}
	count++;

	if (count == 0) {
		cout << "[Pack] No source images found!" << endl;
		return false;
	}
	
	int n_max = nextPow2(count);
	
	int n_h = sqrt(n_max);
	int n_w;
	if (n_h*n_h != n_max) {
		n_h = sqrt(n_max / 2);
		n_w = n_h * 2;
	}
	else {
		n_w = n_h;
	}

	if (pad)
		img_size *= 2;
	
	cout << "[Pack] Atlas dimensions are " << n_w << " x " << n_h << " tiles / " << n_w*img_size << " x " << n_h*img_size << " pixels." << endl;

	CVTFFile vtf_file;
	vtf_file.Create(n_w*img_size, n_h*img_size, 1, 1, 1, IMAGE_FORMAT_DXT5, true, !simple);
	vtf_file.SetFlag(TEXTUREFLAGS_POINTSAMPLE, simple);

	int miplevels = 1;

	if (!simple) // Use the mip count for our individual textures so we get a minimum of 1 pixel per texture.
		miplevels = CVTFFile::ComputeMipmapCount(img_size,img_size, 1);

	for (int i = 0; i < miplevels; i++) {
		double fraction = pow(2, -i);
		int mip_size = img_size*fraction;
		
		PackerImage dst;
		dst.init(n_w*mip_size, n_h*mip_size);

		auto img_iter = images.begin();

		while (img_iter != images.end()) {
			int index = img_iter->first;
			int ix = index % n_w;
			int iy = index / n_w;
			dst.pasteRegion(img_iter->second, ix*mip_size, iy*mip_size, fraction);
			img_iter++;
		}

		if (simple)
			cout << "[Pack] Composed texture." << endl;
		else
			cout << "[Pack] Composed mip level " << i << "." << endl;

		if (!dst.writeTextureData(vtf_file, i)) {
			cout << "[Pack] Failed to convert data to VTF format." << endl;
			return false;
		}
	}

	if (!vtf_file.Save(out_file)) {
		cout << "[Pack] Failed to save texture." << endl;
		return false;
	}

	cout << "[Pack] Success! Texture saved!" << endl;
	return true;
}

void list_tasks() {
	cout << "Valid tasks are:\n\tpack - Pack images into an atlas texture.\n\tslice - Slice an atlas texture into individual images." << endl;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		cout << "No task supplied." << endl;
		list_tasks();
		return 1;
	}

	bool task_success;

	if (strcmp(argv[1], "pack")==0) {
		if (argc < 4) {
			cout << "Invalid arguments for pack. Syntax:" << endl;
			cout << "voxtex pack source_dir dest_file [-simple]" << endl;
			cout << "\tsource_dir - The directory to load PNG images from." << endl;
			cout << "\tdest_file - The name of the file to save. It should be a VTF file." << endl;
			cout << "\t-simple - Make a point-sampled texture and don't generate mipmaps." << endl;
			return 1;
		}
		bool simple = false;
		bool pad = false;
		
		for (int i = 4; i < argc; i++) {
			if (strcmp(argv[i], "-simple") == 0) {
				simple = true;
			}
			else if (strcmp(argv[i], "-pad") == 0) {
				pad = true;
			}
			else {
				cout << "Invalid argument: " << argv[i] << endl;
				return 1;
			}
		}
		
		return !taskPack(argv[2], argv[3], simple, pad);
	}
	else if (strcmp(argv[1], "slice") == 0) {
		if (argc < 6) {
			cout << "Invalid arguments for slice. Syntax:" << endl;
			cout << "voxtex slive source_file dest_dir w h" << endl;
			cout << "\tsource_file - The PNG file to slice apart." << endl;
			cout << "\tdest_dir - The directory to save images in. The directory must exist." << endl;
			cout << "\tw - Number of slices to make on the x axis." << endl;
			cout << "\th - Number of slices to make on the y axis." << endl;
			return 1;
		}

		int w = atoi(argv[4]);
		int h = atoi(argv[5]);

		if (w <= 0 || h <= 0) {
			cout << "Arguments w and h must be integers greater than zero." << endl;
			return 1;
		}

		return !taskSlice(argv[2],argv[3],w,h);
	}
	else {
		cout << "Invalid task supplied." << endl;
		list_tasks();
		return 1;
	}

	//return 0;
	//taskSlice("voxel_test_atlas.png", "src/pdan", 8, 8);

	//taskPack("src/tiny/", "derp.vtf", true);

	/*bool simple_mode = true;

	CVTFFile vtf_file;
	vtf_file.Create(256, 256, 1, 1, 1, IMAGE_FORMAT_DXT5, true, !simple_mode);
	vtf_file.SetFlag(TEXTUREFLAGS_POINTSAMPLE, simple_mode);

	PackerImage img;

	if (img.loadPNG("test.png")) {
		if (img.writeTextureData(vtf_file)) {
			if (vtf_file.Save("test.vtf")) {
				cout << "Success!" << endl;
			}
			else {
				cout << "Save failed!" << endl;
			}
		}
		else {
			cout << "Conversion to VTF failed!" << endl;
		}
	}
	else {
		cout << "Load failed!" << endl;
	}*/

	/*if (load_png("test.png", img_data, img_w, img_h)) {
		cout << "img good ( " << img_w << " x " << img_h << " )" << endl;
		vlByte* dxt_formatted = new vlByte[img_w*img_h];
		if (CVTFFile::ConvertFromRGBA8888(img_data.data(), dxt_formatted, img_w, img_h, IMAGE_FORMAT_DXT5)) {
			cout << "img converted" << endl;
			file.Create(256, 256, 1, 1, 1, IMAGE_FORMAT_DXT5, true, use_mips, true);
			file.SetFlag(TEXTUREFLAGS_POINTSAMPLE, !use_mips);
			file.SetData(0, 0, 0, miplevel, dxt_formatted);
			file.Save("test.vtf");
		}
		else {
			cout << "img conversion failed!" << endl;
		}
	}
	else
		cout << "img bad" << endl;*/
}