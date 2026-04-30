# ND104 Update Weather Screen

Update Chilkey's ND104 keyboard screen using Weather API in Linux

## Pre-requisites

1. A functional NodeJs environment. Please follow [these instructions](https://nodejs.org/en/download).
2. A Weather API key (free). Get yours [here](https://www.weatherapi.com/signup.aspx). 

## Installation:

Just download the repo to any folder of your choice using the following command:

```bash
git clone https://github.com/nelson777/nd104-update-weather .
```

## Usage

```bash
chmod +x nd104-update-weather.js
sudo -E WEATHER_API_KEY=<your_api_key> -E ./nd104-update-weather.js 
```

## Contact

Feel free to contact me: nelson777@gmail.com

## License

[MIT](https://choosealicense.com/licenses/mit/)
