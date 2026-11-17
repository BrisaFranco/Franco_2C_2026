from flask import Flask, render_template

app = Flask(__name__)

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/rojo')
def rojo():
    return render_template('resultado.html', nombre='ROJO', color='#d62828')

@app.route('/verde')
def verde():
    return render_template('resultado.html', nombre='VERDE', color='#2a9d4b')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)