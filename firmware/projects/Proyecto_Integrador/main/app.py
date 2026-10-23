from flask import Flask, render_template

app = Flask(__name__)

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/vista2')
def vista2():
    return render_template('vista2.html')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)