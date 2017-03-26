/** ROS Node implementation of the ursa_driver package.
 \file      ursa_node.cpp
 \authors   Mike Hosmar <mikehosmar@gmail.com>
 \copyright Copyright (c) 2015, Michael Hosmar, All rights reserved.

 The MIT License (MIT)

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#include "ursa_driver/ursa_driver.h"
#include "ros/ros.h"
#include "ursa_driver/ursa_counts.h"
#include "ursa_driver/ursa_spectra.h"
#include "std_srvs/Empty.h"
#include <std_msgs/String.h>

int32_t baud;
std::string port = "";
std::string detector_frame = "";

int HV = 0;
double gain = 0;
int threshold = 0;
double shaping_time = 1;
ursa::shaping_time real_shaping_time;
std::string input_polarity = "";
ursa::inputs real_input;
int ramp = 6;

std::map<double, ursa::shaping_time> shape_map;
std::map<std::string, ursa::inputs> input_map;

bool load_prev;
bool GMmode;
bool imeadiate;

void fill_maps();
int get_params(ros::NodeHandle nh);
void timerCallback(const ros::TimerEvent& event);
bool startAcquireCB(std_srvs::Empty::Request& request,
                    std_srvs::Empty::Response& response);
bool stopAcquireCB(std_srvs::Empty::Request& request,
                   std_srvs::Empty::Response& response);
bool clearSpectraCB(std_srvs::Empty::Request& request,
                    std_srvs::Empty::Response& response);

ursa::Interface * my_ursa;
ros::Publisher publisher;
ros::Timer timer;

int main(int argc, char **argv) {
  ros::init(argc, argv, "ursa_driver");
  ros::NodeHandle nh("~");

  if (!get_params(nh))
    return (-1);

  my_ursa = new ursa::Interface(port.c_str(), baud);
  my_ursa->connect();
  if (my_ursa->connected())
    ROS_INFO("URSA Connected");
  else
    return (-1);

  if (GMmode)
    publisher = nh.advertise<ursa_driver::ursa_counts>("counts", 10);
  else
    publisher = nh.advertise<ursa_driver::ursa_spectra>("spectra", 10);

  ros::ServiceServer startSrv = nh.advertiseService("startAcquire",
                                                    startAcquireCB);
  ros::ServiceServer stopSrv = nh.advertiseService("stopAcquire",
                                                   stopAcquireCB);
  ros::ServiceServer spectraSrv = nh.advertiseService("clearSpectra",
                                                      clearSpectraCB);
  timer = nh.createTimer(ros::Duration(1.0), timerCallback, false, false); //!< @todo look at createWallTimer

  if (load_prev)
  {
    my_ursa->loadPrevSettings();
  }
  else
  {
    my_ursa->setGain(gain);
    my_ursa->setThresholdOffset(threshold);
    my_ursa->setShapingTime(real_shaping_time);
    my_ursa->setInput(real_input);
    my_ursa->setRamp(ramp);
    my_ursa->setVoltage(HV);
  }

  if (imeadiate)
  {
    if (GMmode)
      my_ursa->startGM();
    else
      my_ursa->startAcquire();
    timer.start();
  }

  ros::spin();
  my_ursa->stopAcquire();
  my_ursa->setVoltage(0);
}

bool startAcquireCB(std_srvs::Empty::Request& request,
                    std_srvs::Empty::Response& response) {
  if (GMmode)
    my_ursa->startGM();
  else
    my_ursa->startAcquire();
  timer.start();
  return (true);
}

bool stopAcquireCB(std_srvs::Empty::Request& request,
                   std_srvs::Empty::Response& response) {
  timer.stop();
  if (GMmode)
    my_ursa->stopGM();
  else
    my_ursa->stopAcquire();
  return (true);
}

bool clearSpectraCB(std_srvs::Empty::Request& request,
                    std_srvs::Empty::Response& response) {
  my_ursa->clearSpectra();
  return (true);
}

void timerCallback(const ros::TimerEvent& event) {
  ROS_DEBUG("Hit timer callback.");
  ros::Time now = ros::Time::now();
  if (GMmode)
  {
    ursa_driver::ursa_counts temp;
    temp.header.stamp = now;
    temp.header.frame_id = detector_frame;
    temp.counts = my_ursa->requestCounts();
    publisher.publish(temp);
  }
  else
  {
    ursa_driver::ursa_spectra temp;
    temp.header.stamp = now;
    temp.header.frame_id = detector_frame;
    my_ursa->read();
    my_ursa->getSpectra(&temp.bins);
    publisher.publish(temp);
  }
}

int get_params(ros::NodeHandle nh) {
  nh.param("load_previous_settings", load_prev, false);

  if (!load_prev)
  {
    if (!nh.getParam("high_voltage", HV))
    {
      ROS_ERROR("High voltage must be set.");
      return (-1);
    }
    if (!nh.getParam("gain", gain))
    {
      ROS_ERROR("Gain must be set.");
      return (-1);
    }
    if (!nh.getParam("threshold", threshold))
    {
      ROS_ERROR("Threshold must be set.");
      return (-1);
    }
    if (!nh.getParam("shaping_time", shaping_time))
    {
      ROS_ERROR("Shaping time must be set.");
      return (-1);
    }
    if (!nh.getParam("input_and_polarity", input_polarity))
    {
      ROS_ERROR("Input and Polartity must be set.");
      return (-1);
    }
    if (!nh.getParam("ramping_time", ramp))
    {
      ROS_ERROR("Ramping time must be set.");
      return (-1);
    }

    fill_maps();

    if (shape_map.find(shaping_time) == shape_map.end())
    {
      ROS_ERROR("Shaping time must be valid. Input as double in microseconds.");
      return (-1);
    }

    if (input_map.find(input_polarity) == input_map.end())
    {
      ROS_ERROR(
          "Input and polarity must be valid. Input as string in the format \"intput1_negative\""
          " if using a pre-shaped positive input \"shaped_input\".");
      return (-1);
    }

    real_shaping_time = shape_map[shaping_time];
    real_input = input_map[input_polarity];
  }

  nh.param<std::string>("port", port, "/dev/ttyUSB0");
  nh.param("baud", baud, 115200);

  nh.param("use_GM_mode", GMmode, false);
  nh.param("imeadiate_mode", imeadiate, false);
  nh.param<std::string>("detector_frame", detector_frame, "rad_link");
  return (1);
}

void fill_maps() {
  shape_map[0.25] = ursa::TIME0_25uS;
  shape_map[0.5] = ursa::TIME0_5uS;
  shape_map[1] = ursa::TIME1uS;
  shape_map[2] = ursa::TIME2uS;
  shape_map[4] = ursa::TIME4uS;
  shape_map[6] = ursa::TIME6uS;
  shape_map[8] = ursa::TIME8uS;
  shape_map[10] = ursa::TIME10uS;

  input_map["input1_negative"] = ursa::INPUT1NEG;
  input_map["input1_positive"] = ursa::INPUT1POS;
  input_map["input2_negative"] = ursa::INPUT2NEG;
  input_map["input2_positive"] = ursa::INPUT1POS;
  input_map["shaped_input"] = ursa::INPUTXPOS;
}

