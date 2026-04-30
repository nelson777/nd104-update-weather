# ND104 Update Weather Screen

Update Chilkey's ND104 keyboard screen using Weather API in Linux

## Pre-requisites

1. A Weather API key (free). Get yours [here](https://www.weatherapi.com/signup.aspx). 

2. For the NodeJs Verison
    - A functional NodeJs environment. Please follow [these instructions](https://nodejs.org/en/download).

   For the C++ version:
    - a proper compilation environment. This will suffice: `sudo apt install g++ libcurl4-openssl-dev nlohmann-json3-dev`

## Installation:

Just download the repo to any folder of your choice using the following command:

## Installation:

- Download the repo to any folder of your choice using the following command:

```bash
git clone https://github.com/nelson777/nd104-update-weather .
```
- Pick one of the versions below

#### NodeJs version

```bash
chmod +x nd104-update-weather.js
```

This will make the NodeJs script executable

#### C++ version

Compile it like this:

```bash
g++ -std=c++17 -Wall -Wextra -O2 nd104-update-weather.cpp -o nd104-update-weather -lcurl
```
You'll get the executable file **nd104-update-weather**

## Usage

#### NodeJs version

```bash
WEATHER_API_KEY=<your_weather_api_key> ./nd104-update-weather.js
```


#### C++ version

```bash
WEATHER_API_KEY=<your_weather_api_key> ./nd104-update-weather
```

## Contact

Feel free to contact me: nelson777@gmail.com

## License

[MIT](https://choosealicense.com/licenses/mit/)
```bash
git clone https://github.com/nelson777/nd104-update-weather .
```

