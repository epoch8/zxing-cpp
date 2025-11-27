#!/bin/bash

# Цвета для вывода (для красоты и заметности)
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color
 
FILE_1_PATH="vendor/opencv2.xcframework/ios-arm64/opencv2.framework/Versions/A/opencv2"
FILE_1_URL="https://drive.google.com/file/d/1wpYb2x_BIeyTCxti4_mZLsfLvQz_VfbH/view?usp=sharing"

FILE_2_PATH="vendor/opencv2.xcframework/ios-arm64_x86_64-simulator/opencv2.framework/Versions/A/opencv2"
FILE_2_URL="https://drive.google.com/file/d/1GRMD_aDPTsssbxyR3l19zob4ey5mV9_E/view?usp=sharing"

MISSING_FILES=0

# --- Функция проверки ---

check_file() {
    local path=$1
    local url=$2
    local name=$3

    if [ -f "$path" ]; then
        echo -e "${GREEN}✅ Найден:${NC} $name"
    else
        echo -e "${RED}❌ ОТСУТСТВУЕТ:${NC} $name"
        echo "   Куда положить: $path"
        echo "   Откуда скачать: $url"
        echo "---------------------------------------------------"
        MISSING_FILES=1
    fi
}

# --- Запуск ---

echo "--- Проверка наличия библиотек OpenCV ---"

check_file "$FILE_1_PATH" "$FILE_1_URL" "OpenCV (iOS Arm64)"
check_file "$FILE_2_PATH" "$FILE_2_URL" "OpenCV (Simulator)"

if [ $MISSING_FILES -ne 0 ]; then
    echo -e "${RED}⛔️ ОШИБКА: Не все библиотеки найдены.${NC}"
    echo "Пожалуйста, скачайте недостающие файлы по ссылкам выше и поместите их в указанные папки.  Все данные из папки vendor можно найти так же здесь https://www.notion.so/epoch8/2b8facc20beb8063be68c9f9a3817921"
    exit 1
else
    echo -e "${GREEN}🎉 Все файлы на месте. Сборку можно продолжать.${NC}"
    exit 0
fi