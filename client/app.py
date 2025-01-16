from flask import Flask, render_template
import requests
from plotter import plot_temperature  # Импортируем функцию для построения графиков

app = Flask(__name__)

# Адрес сервера
SERVER_URL = "http://127.0.0.1:8080"

def fetch_data(endpoint):
    """Функция для получения данных с сервера."""
    try:
        response = requests.get(f"{SERVER_URL}/{endpoint}")
        if response.status_code == 200:
            data = response.json()
            for entry in data["data"]:
                entry["value"] = float(entry["value"])  # Преобразуем строку в число
            return data  # Парсим JSON
        else:
            return None
    except requests.exceptions.RequestException as e:
        print(f"Error fetching data from server: {e}")
        return None

@app.route('/')
def index():
    """Главная страница с данными из всех таблиц."""
    # Получаем данные из всех таблиц
    temperatures = fetch_data("temperatures")
    avg_temp_hour = fetch_data("avg_temp_hour")
    avg_temp_day = fetch_data("avg_temp_day")

    # Строим графики
    temp_plot = plot_temperature(temperatures, "Raw Temperatures") if temperatures else None
    avg_hour_plot = plot_temperature(avg_temp_hour, "Average Temperature per Hour") if avg_temp_hour else None
    avg_day_plot = plot_temperature(avg_temp_day, "Average Temperature per Day") if avg_temp_day else None

    return render_template(
        'index.html',
        temp_plot=temp_plot,
        avg_hour_plot=avg_hour_plot,
        avg_day_plot=avg_day_plot
    )

@app.route('/temperatures')
def show_temperatures():
    """Страница с данными из таблицы temperatures."""
    temperatures = fetch_data("temperatures")
    return render_template('temperatures.html', data=temperatures["data"] if temperatures else None)

@app.route('/avg_temp_hour')
def show_avg_temp_hour():
    """Страница с данными из таблицы avg_temp_hour."""
    avg_temp_hour = fetch_data("avg_temp_hour")
    return render_template('avg_temp_hour.html', data=avg_temp_hour["data"] if avg_temp_hour else None)

@app.route('/avg_temp_day')
def show_avg_temp_day():
    """Страница с данными из таблицы avg_temp_day."""
    avg_temp_day = fetch_data("avg_temp_day")
    return render_template('avg_temp_day.html', data=avg_temp_day["data"] if avg_temp_day else None)

if __name__ == '__main__':
    app.run(debug=True)