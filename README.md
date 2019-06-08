## Smart Video Speedup

ffmpeg wrapper to speed up silent parts of videos

## Inspiration

[carykh - Automatic on-the-fly video editing tool!](https://www.youtube.com/watch?v=DQ8orIurGxw)

## Efficiency

As of right now this is the biggest issue. After the first version it was taking nearly double the amount of time of the video I was trying to speed up. Which is not ideal... So the development branch is nearly exclusivly for testing new solutions.

There is also a need to reduce space of converted & cut videos, so that is on the list of things to do.

# Efficiency Ideas

1. Switch video codec to something that encodes faster (.ts?)
2. Break video into frames and speed up audio then re-combine
3. Anyone else got any fast ideas? (Feel free to pull)

## How it works as of now

1. Normalized Video Audio
2. Generate ffmpeg silence output and save it in tempSilence.txt
3. Parse tempSilence.txt and cut video into subclips
4. Speed up subclips as needed and save it in convertedVideo dir
5. Join converted subclips as output

## Build Command

`drive -g driver.cpp -std=c++17 -lstdc++fs`
