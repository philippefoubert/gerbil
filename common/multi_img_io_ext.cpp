/*	
	Copyright(c) 2010 Johannes Jordan <johannes.jordan@cs.fau.de>.

	This file may be licensed under the terms of of the GNU General Public
	License, version 3, as published by the Free Software Foundation. You can
	find it here: http://www.gnu.org/licenses/gpl.html
*/

#include <multi_img.h>
#include <highgui.h>

#ifdef WITH_BOOST
	#include "boost/filesystem.hpp"
#else
	#include "libgen.h"
#endif

#include <fstream>

struct hackstream : public std::ifstream {
	hackstream(const char* in, std::_Ios_Openmode mode)	: ifstream(in, mode) {}
	void readus(unsigned short &d)
	{	read((char*)&d, sizeof(unsigned short));	}
	void readui(unsigned int &d)
	{	read((char*)&d, sizeof(unsigned int));		}
	void reada(unsigned char *a, size_t num)
	{	read((char*)a, sizeof(unsigned char)*num);	}
	void reada(unsigned short *a, size_t num)
	{	read((char*)a, sizeof(unsigned short)*num);	}
};

bool multi_img::read_image_lan(const string& filename)
{
	// we omit checks for data consistency
	assert(empty());

	// parse header
	unsigned short depth, size;
	unsigned int rows, cols;
	hackstream in(filename.c_str(), ios::in | ios::binary);
	char buf[7] = "123456"; // enforce trailing \0
	in.read(buf, 6);
	if (strcmp(buf, "HEADER") && strcmp(buf, "HEAD74")) {
		return false;
	}

	in.readus(depth);
	in.readus(size);
	in.seekg(16);
	in.readui(cols);
	in.readui(rows);
	in.seekg(128);

	if (depth != 0 && depth != 2) {
		std::cout << "Data format not supported yet."
				" Please send us your file!" << std::endl;
		return false;
	}

	std::cout << "Total of " << size << " bands. "
	             "Spatial size: " << cols << "x" << rows
	          << "\t(" << (depth == 0 ? "8" : "16") << " bits)" << std::endl;

	// prepare image
	init(cols, rows, size);

	/* read data in BIL (band interleaved by line) format */
	// we use either the 8bit or the 16bit array, have both for convenience. */
	unsigned char *srow8 = new unsigned char[cols];
	unsigned short *srow16 = new unsigned short[cols];
	for (unsigned int y = 0; y < rows; ++y) {
		for (int d = 0; d < size; ++d) {
			multi_img::Value *drow = bands[d][y];
			if (depth == 0)
				in.reada(srow8, cols);
			else
				in.reada(srow16, cols);
			for (unsigned int x = 0; x < cols; ++x)
				drow[x] = (Value)(depth == 0 ? srow8[x] : srow16[x]);
		}
	}
	delete[] srow8;
	delete[] srow16;
	in.close();

	/* rescale data according to minval/maxval */
	double srcmaxval = (depth == 0 ? 255. : 65535.);
	Value scale = (maxval - minval)/srcmaxval;
	for (int d = 0; d < size; ++d) {
		if (minval != 0.) {
			bands[d] = bands[d] * scale + minval;
		} else {
			if (maxval != srcmaxval) // evil comp., doesn't hurt
				bands[d] *= scale;
		}
	}
	return true;
}

void multi_img::write_out(const string& base, bool normalize) const
{
    ofstream txtfile((base + ".txt").c_str());
    txtfile << size() << "\n";
    txtfile << "./" << "\n";

	char name[1024];
	for (size_t i = 0; i < size(); ++i) {
		sprintf(name, "%s%02d.png", base.c_str(), (int)i);

		/// write only the basename in the text file
		txtfile << string(name).substr(string(name).find_last_of("/") + 1);
		txtfile << " " << meta[i].rangeStart;
		if (meta[i].rangeStart != meta[i].rangeEnd) /// print range, if available
			txtfile << " "  << meta[i].rangeStart;
		txtfile << "\n";

		if (normalize) {
			cv::Mat_<uchar> normalized;
			Value scale = 255./(maxval - minval);
			bands[i].convertTo(normalized, CV_8U, scale, -scale*minval);
			cv::imwrite(name, normalized);
		} else
			cv::imwrite(name, bands[i]);
	}

    txtfile.close();
}

// parse file list
pair<vector<string>, vector<multi_img::BandDesc> >
		multi_img::parse_filelist(const string& filename)
{
	pair<vector<string>, vector<BandDesc> > empty;

	ifstream in(filename.c_str());
	if (in.fail())
		return empty;

	unsigned int count;
	string base;
	in >> count;
	in >> base;
	if (in.fail())
		return empty;

#ifdef WITH_BOOST
	boost::filesystem::path basepath(base), filepath(filename);
	if (!basepath.is_complete()) {
		basepath = filepath.remove_leaf() /= basepath;
		base = basepath.string();
	}
#elif __unix__
	if (base[0] != '/') {
		char *f = strdup(filename.c_str()), *d = dirname(f);
		base = string(d).append("/").append(base);
	}
#else
	std::cerr << "Warning: only absolute file paths accepted." << std::endl;
#endif
	base.append("/"); // TODO: check if o.k. in Windows
	stringstream in2;
	string fn; float a, b;
	vector<string> files;
	vector<BandDesc> descs;
	in >> ws;
	for (; count > 0; count--) {
		in2.clear();
		in.get(*in2.rdbuf()); in >> ws;
		in2 >> fn;
		if (in2.fail()) {
			cerr << "fail!" << endl;
			return empty;	 // file inconsistent -> screw it!
		}
		files.push_back(base + fn);

		in2 >> a;
		if (in2.fail()) { // no band desc given
			descs.push_back(BandDesc());
		} else {
			in2 >> b;
			if (in2.fail()) // only center filter wavelength given
				descs.push_back(BandDesc(a));
			else			// filter borders given
				descs.push_back(BandDesc(a, b));
		}
	}
	in.close();
	return make_pair(files, descs);
}
