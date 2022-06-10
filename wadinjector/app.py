import argparse
import fcntl
import os
import subprocess
import tempfile
from contextlib import contextmanager
from flask import Flask, Response, flash, request, make_response, render_template
from werkzeug.utils import secure_filename

UPLOAD_FOLDER = '/path/to/the/uploads'
ALLOWED_EXTENSIONS = {'wad'}

app = Flask(__name__, template_folder=os.path.dirname(os.path.abspath(__file__)))
app.config['MAX_CONTENT_LENGTH'] = 32 * 1024 * 1024


def allowed_file(filename: str) -> bool:
    return '.' in filename and \
        filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS


@contextmanager
def repo_lock(lockfile: str):
    locked_file_descriptor = open(lockfile, 'w+')
    fcntl.lockf(locked_file_descriptor, fcntl.LOCK_EX)
    try:
        yield
    finally:
        locked_file_descriptor.close()


@app.route('/', methods=["GET"])
def index() -> Response:
    return render_template("index.html")


@app.errorhandler(413)
def request_entity_too_large(error):
    flash('File too large!')
    return render_template("index.html")


@app.route('/', methods=["POST"])
def wadupload() -> Response:
    if 'filename' not in request.files:
        flash('No file specified!')
        return render_template("index.html")

    f = request.files['filename']
    if not f or f.filename is None or f.filename == '':
        flash('No file specified!')
        return render_template("index.html")

    if not allowed_file(f.filename):
        flash('Invalid file type!')
        return render_template("index.html")

    # Grabbed the wad!
    filename = secure_filename(f.filename).upper()
    fullname = os.path.join(app.config['UPLOAD_FOLDER'], filename)
    f.save(fullname)

    # We reuse the cache between compiles, so don't let multiple things build at once.
    with repo_lock(app.config['LOCKFILE']):
        # Now, build it!
        try:
            output = subprocess.check_output(
                """
                    set -e
                    source /opt/toolchains/naomi/env.sh
                    cd $GITHUB_ROOT
                    git pull
                    make customwad
                    rm $WADFILE
                """,
                shell=True,
                executable="/bin/bash",
                stderr=subprocess.STDOUT,
                env={
                    "GITHUB_ROOT": app.config['GITHUB'],
                    "WADFILE": fullname,
                }
            )

            if b"Custom ROM build and moved to doom-custom.bin!" not in output:
                flash("Failed to inject WAD!")
                return render_template("index.html")

            with open(os.path.join(app.config['GITHUB'], 'doom-custom.bin'), "rb") as bfp:
                response = make_response(bfp.read())
                response.headers.set('Content-Type', 'application/octet-stream')
                response.headers.set('Content-Disposition', 'attachment', filename='doom-custom.bin')
                return response
        except subprocess.CalledProcessError:
            flash("Failed to inject WAD!")
            return render_template("index.html")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="A simple flask server to accept WAD files via POST and return a Naomi Doom binary with that WAD injected.")
    parser.add_argument("-p", "--port", help="Port to listen on. Defaults to 5678", type=int, default=5678)
    parser.add_argument("-d", "--debug", help="Enable debug mode. Defaults to off", action="store_true")
    parser.add_argument("-g", "--github", help="Root of the github repo. Defaults to super directory", type=str, default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmpdirname:
        githubdir = os.path.abspath(args.github)
        print(f"File uploads will go to {tmpdirname}")
        print(f"Building doom executables out of {githubdir}")
        app.config['UPLOAD_FOLDER'] = tmpdirname
        app.config['SECRET_KEY'] = tmpdirname
        app.config['GITHUB'] = githubdir
        app.config['LOCKFILE'] = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lock.file')
        app.run(host='0.0.0.0', port=args.port, debug=args.debug)
