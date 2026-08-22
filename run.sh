#!/bin/sh
# Generate a config with this clone's absolute paths (the parser accepts absolute
# paths only), then start the server on :8080.
cd "$(dirname "$0")" || exit 1
sed "s|@ROOT@|$(pwd)|g" config/webserv.conf.template > config/webserv.conf
exec ./webserv config/webserv.conf
