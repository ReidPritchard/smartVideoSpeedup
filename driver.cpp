#include <stdlib.h>
#include <algorithm>  // std::sort
#include <chrono>
#include <cstdlib> /* system, NULL, EXIT_FAILURE */
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace cs = std::chrono;

///////////////////////////////////////////////////

class silentTimestamp {
 private:
  double startTime;
  double endTime;

 public:
  silentTimestamp() {
    startTime = 0.0;
    endTime = 0.0;
  }
  void setStartTime(double inTime) { startTime = inTime; }
  void setEndTime(double inTime) { endTime = inTime; }
  double getStartTime() { return startTime; }
  double getEndTime() { return endTime; }
  double getDuration() { return endTime - startTime; }
  ~silentTimestamp() {
    startTime = 0.0;
    endTime = 0.0;
  }
};

///////////////////////////////////////////////////

int normalizeAudio(fs::path &fileName);
int detectSilence(fs::path videoFileName, int noiseThreshold, std::string outputFileName);
int parseSilence(std::string silenceFile, std::vector<silentTimestamp> &times);
int splitVideo(fs::path videoFileName, std::string outputFilePath, std::vector<silentTimestamp> times);
int splitVideoHelper(fs::path videoFileName, std::string outputFilePath, silentTimestamp timestamp, int &iter, bool toFast);
int speedVideos(std::string outputFilePath, float siSpeed, float normSpeed);
int joinConverted(fs::path videoFileName, std::string outputFilepath);
int cleanDir(std::string outputFilepath, std::string tempSi, fs::path videoFilePath);
int errorLog(int status, std::string commmand);

bool verbose = true;
bool printFFMPEG = false;

///////////////////////////////////////////////////

int main(int argc, char *argv[]) {
  if (argc == 5) {
    int status;
    std::vector<silentTimestamp> times;
    fs::path videoFileName(argv[1]);
    int silenceThreshold = atoi(argv[2]);
    const std::string tempSilenceFilename = "tempSilence.txt";
    fs::path tempSilenceFilenamePath(tempSilenceFilename);
    const std::string tempVideoOutputFilePath = "./tempVideos";
    fs::path tempVideoOutputFilePath_boostPathVar(tempVideoOutputFilePath);
    float siSpeed = atof(argv[3]);
    float normSpeed = atof(argv[4]);

    auto start = cs::high_resolution_clock::now();

    if (cleanDir(tempVideoOutputFilePath, tempSilenceFilename, videoFileName)) return -1;

    if (normalizeAudio(videoFileName))
      return -1;
    else
      std::cout << "/////////////////// Audio Normalized ///////////////////" << std::endl;

    if (detectSilence(videoFileName, silenceThreshold, tempSilenceFilename))
      return -1;
    else
      std::cout << "/////////////////// Silence Detected ///////////////////" << std::endl;

    if (parseSilence(tempSilenceFilename, times))
      return -1;
    else
      std::cout << "/////////////////// Silence Parsed ///////////////////" << std::endl;

    if (splitVideo(videoFileName, tempVideoOutputFilePath, times))
      return -1;
    else
      std::cout << "/////////////////// Split Video ///////////////////" << std::endl;

    if (speedVideos(tempVideoOutputFilePath, siSpeed, normSpeed))
      return -1;
    else
      std::cout << "/////////////////// Video's Sped up ///////////////////" << std::endl;

    if (joinConverted(videoFileName, tempVideoOutputFilePath))
      return -1;
    else
      std::cout << "/////////////////// New Video Created! YAY!! ///////////////////" << std::endl;

    status = cleanDir(tempVideoOutputFilePath, tempSilenceFilename, videoFileName);
    if (status != 0) return -1;

    auto stop = cs::high_resolution_clock::now();
    auto duration = cs::duration_cast<cs::seconds>(stop - start);
    std::cout << "Process took: " << duration.count() << " seconds" << std::endl;

  } else {
    std::cout << "Error Processing inputs use format: " << std::endl;
    std::cout << "\t./drive [File Name] [Silence Threshold] [Silence Speed] [Normal Speed]" << std::endl;
    return -1;
  }

  return 0;
}

int normalizeAudio(fs::path &fileName) {
  std::string path;
  if (fileName.parent_path().string().length() > 0) {
    path = fileName.parent_path().string() + "/" + fileName.stem().string();
  } else {
    path = fileName.stem().string();
  }

  const std::string normalizeAudio = "ffmpeg -loglevel fatal -f mp4 -y -i " + fileName.string() +
                                     " -af dynaudnorm -vcodec copy " + path + "-normalized.mp4";

  if (printFFMPEG) std::cout << normalizeAudio << std::endl;

  fileName.replace_extension("");
  fileName.replace_filename(fileName.filename().string() + "-normalized");
  fileName.replace_extension(".mp4");

  return (errorLog(system(normalizeAudio.c_str()), "Audio Normalized") != 0);
}

int detectSilence(fs::path fileName, int noiseThreshold, std::string outputFileName) {
  // Run ffmpeg command to detect silence and > output into outputFileName
  const std::string dSilenceCommand = "ffmpeg -hide_banner -i \"" + fileName.string() + "\" -af silencedetect=noise=-" +
                                      std::to_string(abs(noiseThreshold)) + "dB:d=0.5 -f null - 2> " + outputFileName;

  if (printFFMPEG) std::cout << dSilenceCommand << std::endl;

  return errorLog(system(dSilenceCommand.c_str()), "Detect silence command");
}

int parseSilence(std::string silenceFile, std::vector<silentTimestamp> &times) {
  std::ifstream file(silenceFile);
  std::string str;

  const std::regex regSilence(
      "\\[silencedetect.+] silence_(\\w+)+: (\\d*\\.?\\d*)(?: \\| .+: (.+))?");  // Regex to parse silence file
  std::smatch match;

  silentTimestamp temp = silentTimestamp();

  while (std::getline(file, str)) {                   // for each line in file
    if (std::regex_search(str, match, regSilence)) {  // If line matches regex
      if (match[1].compare("start") == 0) {           // If line is for start of silence
        temp.setStartTime(stod(match[2]));            // Match 2 is start/end time

      } else {                            // If line is for end of silence
        temp.setEndTime(stod(match[2]));  // Match 2 is start/end time
        // Match 3 is the duration of silence (this is not needed as we calculate it on the fly)

        times.push_back(temp);  // Add temp timestamp to vector
      }
    }
  }

  return 0;
}

int splitVideoHelper(fs::path videoFileName, std::string outputFilePath, silentTimestamp timestamp, int &iter,
                     bool isSilence) {
  std::stringstream ss;                               // Create String Stream
  ss << std::setw(4) << std::setfill('0') << iter++;  // Set iter to print as four digits

  std::string splitVideoCommand = "ffmpeg -loglevel fatal -ss " + std::to_string(timestamp.getStartTime()) + " -i \"" +
                                  videoFileName.string() + "\" -t " + std::to_string(timestamp.getDuration()) +
                                  " -n -map 0 -avoid_negative_ts 1 " + outputFilePath + "/" + ss.str();

  // "-ss $startCutTime -i $inputFilePath -ss 0 -c copy -to $endCutTime -avoid_negative_ts make_zero $outputFilePath"

  std::string logMessage = "Split Video to " + ss.str();  // Build Log string
  if (isSilence) {                                        // If file is silence
    splitVideoCommand += "-silence.mp4";                  // Add to split command
    logMessage += "-silence.mp4";                         // Add to log message
  } else {                                                // If video is to be kept at normal speed
    splitVideoCommand += "-norm.mp4";                     // Add to split command
    logMessage += "-norm.mp4";                            // Add to log message
  }

  if (printFFMPEG) std::cout << splitVideoCommand << std::endl;

  return (errorLog(system(splitVideoCommand.c_str()), logMessage));
}

int splitVideo(fs::path videoFileName, std::string outputFilePath, std::vector<silentTimestamp> times) {
  if (fs::exists(outputFilePath)) fs::remove_all(outputFilePath);  // If directory exists remove it
  fs::create_directory(outputFilePath);                            // Create directory

  int iter = 0;
  silentTimestamp norm = silentTimestamp();  // Create temp timestamp for non-silence splitting
  norm.setStartTime(0.0);                    // Set start time to start of video for first split
  norm.setEndTime(times[0].getStartTime());  // Set end time to start of first timestamp

  for (int i = 0; i < times.size() - 1; i++) {  // Loop through all "timestamp" objects
    if (norm.getStartTime() != norm.getEndTime() &&
        splitVideoHelper(videoFileName, outputFilePath, norm, iter, false) != 0)  // split video on non-silence section
      return -1;
    if (splitVideoHelper(videoFileName, outputFilePath, times[i], iter, true) != 0)
      return -1;                                   // Split video on current silent section
    norm.setStartTime(times[i].getEndTime());      // Set start of non-silent timestamp to end of last silence timestamp
    norm.setEndTime(times[i + 1].getStartTime());  // Set end of non-silent timestamp to start of next silence timestamp
  }
  splitVideoHelper(videoFileName, outputFilePath, norm, iter, false);  // split video on non-silence section
  splitVideoHelper(videoFileName, outputFilePath, times[times.size() - 1], iter,
                   true);  // Split video on current silent section

  return 0;
}

int audioSpeedFilterBuilder(std::string &filter, float speed) {
  // Video speed == setpts=PTS/speed
  // Where speed is the factor the video should be sped up (whole number)
  filter = "";
  if (speed > 2) {  // If the video is going to be sped up by a factor > than 2:
    double tempSpeedIter = speed;
    for (int i = 0; i < speed / 2; i++) {
      // Create multiple "atempo=2.0" Filters until the factor is reached (I think by multiplying the filters)
      if (i != 0) filter += ",";
      filter += "atempo=2.0";
      tempSpeedIter = tempSpeedIter / 2;
    }
    if (tempSpeedIter != 1 && tempSpeedIter > .5 && tempSpeedIter < 2) filter += ",atempo=" + std::to_string(tempSpeedIter);
  } else if (speed < .5) {  // If the speed is slower than .5
    // TODO need to fix, but why whould I need slowmo anyway!!

    // build with "atempo=0.5" Filters until the desired factor is reached
  } else {
    filter += "atempo=" + std::to_string(speed);
  }

  return 0;
}

int speedVideosHelper(std::string outputFilePath, fs::path filePath, float speed) {
  // ffmpeg -i slow.mp4 -filter:v "setpts=0.5*PTS" part-1.mp4
  const std::string mvCommand = "mv " + outputFilePath + "/" + filePath.filename().string() + " " + outputFilePath +
                                "/convertedVideos/converted-" + filePath.filename().string();
  if (speed == 1)
    return errorLog(system(mvCommand.c_str()), "Sped Video Moved to - " + outputFilePath + "/convertedVideos/converted-" +
                                                   filePath.filename().string());

  std::string filter;
  audioSpeedFilterBuilder(filter, speed);

  std::string speedCommand = "ffmpeg -loglevel fatal -y -i \"" + filePath.string() +
                             "\" -filter_complex \"[0:v]setpts=PTS/" + std::to_string(speed) + "[v];[0:a]" + filter +
                             "[a]\" -map \"[v]\" -map \"[a]\" -b:v 1500k " + outputFilePath + "/convertedVideos/converted-" +
                             filePath.filename().string();

  if (printFFMPEG) std::cout << speedCommand << std::endl;

  return (errorLog(system(speedCommand.c_str()),
                   "Sped Video to - " + outputFilePath + "/convertedVideos/converted-" + filePath.filename().string()));
}

int speedVideos(std::string outputFilePath, float silenceSpeed, float normSpeed) {
  if (fs::exists(outputFilePath + "/convertedVideos"))
    fs::remove_all(outputFilePath + "/convertedVideos");  // If directory exists remove it
  fs::create_directory(outputFilePath + "/convertedVideos");

  fs::directory_iterator end_itr;  // Default ctor yields past-the-end

  std::regex filenameReg(".+-(\\w+).mp4");
  std::smatch match;

  for (fs::directory_iterator directoryIter(outputFilePath); directoryIter != end_itr; directoryIter++) {
    std::string tmp = directoryIter->path().filename().string();
    if (std::regex_match(tmp, match, filenameReg)) {
      if (match[1].compare("norm") == 0) {  // Idk why but it will only match norm NOT silence... WHY????
        if (speedVideosHelper(outputFilePath, directoryIter->path(), normSpeed) != 0)
          return -1;  // Run speed up with normSpeed
      } else {        // If clip is silence
        if (speedVideosHelper(outputFilePath, directoryIter->path(), silenceSpeed) != 0)
          return -1;  // Run speed up with silence speed
      }
    }
  }
  return 0;
}

int buildJoinFile(std::string outputFilepath) {
  const std::regex convertedRgx(".+converted-.+-(\\w+).mp4");  // Regex to match with converted filenames
  std::smatch match;                                           // Match variable

  std::vector<std::string> filePathsVec;  // Vector of filepaths to be sorted and copied to join.txt later

  fs::directory_iterator end_itr;  // Used to check if end of directory
  for (fs::directory_iterator i(outputFilepath + "/convertedVideos"); i != end_itr; i++) {
    std::string tmp = i->path().string();                                       // Loop through all files in outputFilepath
    if (std::regex_search(tmp, match, convertedRgx)) {                          // if filepath matches converted filename
      filePathsVec.push_back("file \'" + absolute(i->path()).string() + "\'");  // add absolute filenames to vector
    }
  }

  std::sort(filePathsVec.begin(), filePathsVec.end());  // Sort filepaths for joining later

  if (fs::exists("./join.txt")) fs::remove("./join.txt");                 // If file (join.txt) is present delete it
  std::ofstream output_file("./join.txt");                                // Set output file as join.txt
  std::ostream_iterator<std::string> output_iterator(output_file, "\n");  // to be used in copy function
  std::copy(filePathsVec.begin(), filePathsVec.end(), output_iterator);   // Copy contents of vector to join.txt

  return 0;
}

int fixSilentFirstClip(fs::path outputFilepath) {
  int returnValue = 0;

  std::cout << outputFilepath.stem().string() + "/convertedVideos/converted-0000-silence.mp4" << std::endl;

  if (!fs::exists(outputFilepath.stem().string() + "/convertedVideos/converted-0000-silence.mp4")) return -1;

  const std::string audioProbe =
      "ffprobe -i " + outputFilepath.stem().string() +
      "/convertedVideos/converted-0000-silence.mp4 -show_streams -select_streams a -loglevel error > superTemp.txt";

  // std::cout << audioProbe << std::endl;

  errorLog(system(audioProbe.c_str()), "Check file for audio");

  if (fs::is_empty("./superTemp.txt")) {
    // ffmpeg -f lavfi -i anullsrc=channel_layout=stereo:sample_rate=44100 -i video.mov \
  -shortest -c:v copy -c:a aac output.mov
    const std::string addEmptyAudio = "ffmpeg -loglevel fatal -f lavfi -i anullsrc -i " + outputFilepath.string() +
                                      "/convertedVideos/converted-0000-silence.mp4 -shortest -c:v copy -c:a aac " +
                                      outputFilepath.string() + "/convertedVideos/converted-0000-silence-temp.mp4";

    returnValue = errorLog(system(addEmptyAudio.c_str()), "Adding empty audio to first clip");

    std::string mvTempVid = "mv " + outputFilepath.string() + "/convertedVideos/converted-0000-silence-temp.mp4 " +
                            outputFilepath.string() + "/convertedVideos/converted-0000-silence.mp4";

    if (returnValue == 0) errorLog(system(mvTempVid.c_str()), "Rename 0000 tempFile");
  }

  fs::remove("./superTemp.txt");

  return returnValue;
}

int joinConverted(fs::path videoFileName, std::string outputFilepath) {
  if (errorLog(buildJoinFile(outputFilepath), "Build Join.txt") != 0) return -1;
  errorLog(fixSilentFirstClip(outputFilepath), "Check file for audio");

  std::string joinCommand = "ffmpeg -loglevel fatal -y -f concat -safe 0 -i join.txt -fflags +genpts \"" +
                            videoFileName.stem().string() + "-final.mp4\"";

  return (errorLog(system(joinCommand.c_str()), "joinCommand"));
}

int cleanDir(std::string outputFilepath = "", std::string tempSi = "", fs::path videoFilePath = "") {
  if (fs::exists(outputFilepath)) {                                          // If path exists
    errorLog(!fs::remove_all(outputFilepath), "cleaned " + outputFilepath);  // remove it
  }

  if (fs::exists("./" + tempSi)) {                                     // If path exists
    errorLog(!fs::remove("./" + tempSi), "cleaned tempSilence file");  // remove it
  }

  if (fs::exists("./join.txt")) {                             // If path exists
    errorLog(!fs::remove("./join.txt"), "join.txt cleaned");  // remove it
  }

  if (fs::exists("./superTemp.txt")) {                                       // If path exists
    errorLog(!fs::remove("./superTemp.txt"), "cleaned superTemp.txt file");  // remove it
  }

  if (videoFilePath.filename().string().find("-normalized") != std::string::npos &&
      fs::exists(videoFilePath.string())) {                                       // If path exists
    errorLog(!fs::remove(videoFilePath.string()), "cleaned superTemp.txt file");  // remove it
  }

  return 0;
}

int errorLog(int status, std::string commmand) {
  if (status != 0) {
    std::cout << "Error running command - " << commmand << std::endl;
    return -1;
  } else {
    if (verbose) std::cout << "Successful - (" << commmand << ")" << std::endl;
    return 0;
  }
}