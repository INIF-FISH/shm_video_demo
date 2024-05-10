#ifndef _SHM_VIDEO_TRANSMISSION_H
#define _SHM_VIDEO_TRANSMISSION_H

#include <iostream>
#include <opencv2/opencv.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/sync/upgradable_lock.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <chrono>

namespace shm_video_trans
{
    using namespace boost::interprocess;
    using namespace cv;
    using namespace std::chrono;

    struct FrameMetadata
    {
        interprocess_upgradable_mutex mutex;
        high_resolution_clock::time_point time_stamp;
        high_resolution_clock::time_point write_time;
        int width, height;
    };

    struct FrameBag
    {
        high_resolution_clock::time_point time_stamp;
        high_resolution_clock::time_point write_time;
        cv::Mat frame;
    };

    class VideoReceiver
    {
    private:
        std::string channel_name_;
        std::shared_ptr<shared_memory_object> shm_obj;
        std::shared_ptr<mapped_region> region;
        high_resolution_clock::time_point last_receive_time = high_resolution_clock::time_point();

    public:
        VideoReceiver(std::string channel_name)
        {
            channel_name_ = channel_name;
            shm_obj = std::make_shared<shared_memory_object>(open_only, channel_name_.c_str(), read_write);
            region = std::make_shared<mapped_region>(*shm_obj, read_write);
        }

        ~VideoReceiver()
        {
            FrameMetadata *metadata_ptr(new FrameMetadata);
            std::memcpy(static_cast<unsigned char *>(region->get_address()), metadata_ptr, sizeof(FrameMetadata));
        }

        bool receive(FrameBag &out)
        {
            FrameMetadata *metadata = reinterpret_cast<FrameMetadata *>(region->get_address());
            sharable_lock<interprocess_upgradable_mutex> lock(metadata->mutex);
            if (metadata->write_time <= last_receive_time)
                return false;
            out.frame = cv::Mat(metadata->height, metadata->width, CV_8UC3, static_cast<unsigned char *>(region->get_address()) + sizeof(FrameMetadata));
            out.time_stamp = metadata->time_stamp;
            out.write_time = metadata->write_time;
            last_receive_time = metadata->write_time;
            return true;
        }
    };

    class VideoSender
    {
    private:
        int video_width_, video_height_, video_size_;
        std::string channel_name_;
        std::shared_ptr<shared_memory_object> shm_obj;
        std::shared_ptr<mapped_region> region;

    public:
        VideoSender(std::string channel_name, const int video_width, const int video_height)
        {
            video_width_ = video_width;
            video_height_ = video_height;
            video_size_ = video_width * video_height * 3;
            channel_name_ = channel_name;
            shm_obj = std::make_shared<shared_memory_object>(open_or_create, channel_name_.c_str(), read_write);
            shm_obj->truncate(video_size_ + sizeof(FrameMetadata));
            region = std::make_shared<mapped_region>(*shm_obj, read_write);
            FrameMetadata *metadata_ptr(new FrameMetadata);
            std::memcpy(static_cast<unsigned char *>(region->get_address()), metadata_ptr, sizeof(FrameMetadata));
        }

        ~VideoSender()
        {
            shm_obj->remove(channel_name_.c_str());
        }

        void send(cv::Mat &frame, std::chrono::high_resolution_clock::time_point time_stamp = std::chrono::high_resolution_clock::time_point())
        {
            if (frame.cols != video_width_ || frame.rows != video_height_)
                cv::resize(frame, frame, cv::Size(video_width_, video_height_));
            FrameMetadata *metadata = reinterpret_cast<FrameMetadata *>(region->get_address());
            upgradable_lock<interprocess_upgradable_mutex> lock(metadata->mutex);
            std::memcpy(static_cast<unsigned char *>(region->get_address()) + sizeof(FrameMetadata), frame.data, video_size_);
            metadata->height = video_height_;
            metadata->width = video_width_;
            metadata->time_stamp = time_stamp;
            metadata->write_time = high_resolution_clock::now();
        }
    };
} // namespace shm_video_trans

#endif // _SHM_VIDEO_TRANSMISSION_H
