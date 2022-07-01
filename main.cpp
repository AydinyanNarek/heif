#include <iostream>
#include <string>
#include <vector>

#include <libheif/heif.h>

heif_context* reader_ctx = nullptr;
heif_context* writer_ctx = nullptr;
int img_width, img_height, img_channels = 0;
std::vector<uint8_t> image_data;
std::vector<uint8_t> icc_color_profile_data;

void check_err(const heif_error* err) {
	if (!err) {
		return;
	}

	switch (err->code) {
		case heif_error_Ok: {
			break;
		}
		default: {
			throw(err->message);
		}
	}
}

void get_img_handle(heif_image_handle** imgHandle, heif_item_id id) {
	auto err = heif_context_get_image_handle(reader_ctx, id, imgHandle);
	check_err(&err);
}

int get_img_count() { return heif_context_get_number_of_top_level_images(reader_ctx); }

std::vector<heif_item_id> getImageIDs() {
	int imgCount = get_img_count();
	if (!imgCount) {
		throw("DataNotExists");
	}
	std::vector<heif_item_id> ids(imgCount);
	heif_context_get_list_of_top_level_image_IDs(reader_ctx, ids.data(), imgCount);

	return ids;
}

bool has_alpha(const heif_image_handle* handle) { return static_cast<bool>(heif_image_handle_has_alpha_channel(handle)); }

bool has_premultipled_alpha(const heif_image_handle* handle) { return static_cast<bool>(heif_image_handle_is_premultiplied_alpha(handle)); }

heif_chroma get_img_handleColorSpace(const heif_image_handle* handle) {
	if (has_premultipled_alpha(handle)) {
		return heif_chroma_interleaved_RRGGBBAA_BE;
	} else if (has_alpha(handle)) {
		return heif_chroma_interleaved_RGBA;
	}
	return heif_chroma_interleaved_RGB;
}

void decode_image(const std::string& filename) {
	reader_ctx = heif_context_alloc();
	auto err = heif_context_read_from_file(reader_ctx, filename.data(), nullptr);
	check_err(&err);

	const auto imgIds = getImageIDs();
	heif_image_handle* rawHandle = nullptr;
	get_img_handle(&rawHandle, imgIds[0]);

	img_width = heif_image_handle_get_width(rawHandle);
	img_height = heif_image_handle_get_height(rawHandle);
	auto colorSpace = get_img_handleColorSpace(rawHandle);
	img_channels = has_alpha(rawHandle) ? 4 : 3;

	heif_image* rawImg = nullptr;
	heif_decode_image(rawHandle, &rawImg, heif_colorspace_RGB, colorSpace, nullptr);

	int stride = 0;
	const uint8_t* data = heif_image_get_plane_readonly(rawImg, heif_channel_interleaved, &stride);
	if (!data) {
		throw("Failed to extract image.");
	}

	image_data.resize(img_width * img_height * img_channels);
	for (int32_t y = 0; y < img_height; y++) {
		std::copy_n(data + y * stride, img_width * img_channels, image_data.data() + y * img_width * img_channels);
	}

	int64_t size = heif_image_handle_get_raw_color_profile_size(rawHandle);
	if (!size) {
		printf("Image doesn't contain color profile data\n");
	} else {
		icc_color_profile_data.resize(size);
		err = heif_image_handle_get_raw_color_profile(rawHandle, icc_color_profile_data.data());
		check_err(&err);
	}

	heif_image_release(rawImg);
	heif_image_handle_release(rawHandle);
	heif_context_free(reader_ctx);
}

void encode_image() {
	writer_ctx = heif_context_alloc();
	heif_encoder* encoder = nullptr;
	heif_context_get_encoder_for_format(writer_ctx, heif_compression_HEVC, &encoder);
	if (!encoder) {
		throw("Couldn't find matching encoder for encoding image data.");
	}
	heif_encoder_set_lossless(encoder, true);
	heif_image* image = nullptr;

	auto err =
		heif_image_create(img_width, img_height, heif_colorspace_RGB, img_channels == 4 ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB, &image);
	check_err(&err);

	err = heif_image_add_plane(image, heif_channel_interleaved, img_width, img_height, img_channels * 8);
	check_err(&err);

	int stride;
	uint8_t* const p = heif_image_get_plane(image, heif_channel_interleaved, &stride);
	if (!p) {
		throw("faild to write image to the buffer");
	}

	for (int y = 0; y < img_height; y++) {
		std::copy_n(image_data.data() + y * img_width * img_channels, img_width * img_channels, p + y * stride);
	}

	heif_image_handle* imageHandle = nullptr;
	err = heif_context_encode_image(writer_ctx, image, encoder, nullptr, &imageHandle);
	check_err(&err);

	if (icc_color_profile_data.empty()) {
		err = heif_image_set_raw_color_profile(image, "rICC", icc_color_profile_data.data(), icc_color_profile_data.size());
		check_err(&err);
	}

	heif_context_write_to_file(writer_ctx, "out.heic");

	heif_image_handle_release(imageHandle);
	heif_image_release(image);
	heif_encoder_release(encoder);
	heif_context_free(writer_ctx);
}

// I don't need to handle multiframe images, so this will work only for single frame .heic files
int main() try {
    std::cout << "Enter path of an input image file\n";
    std::string filename;
    std::cin >> filename;

	decode_image(filename);
	encode_image();
} 
catch (const std::exception& e) {
	std::cerr << e.what();
} catch (const char* e) {
	std::cerr << e;
} catch (const std::string& e) {
	std::cerr << e;
} catch (...) {
	std::cerr << "unknown exception occured\n";
}
