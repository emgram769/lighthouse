#!/usr/bin/python3
import requests
import json
import argparse

URL = "https://searx.me/?format=json&q="

def searx(query, settings):
    """
    """
    try:
        request = requests.request("GET", URL + query.replace(" ", "+"))

        results = json.loads(request.text)["results"]
    except:
        # If no connection or no query.
        return ''

    res = []
    for i in range(min(len(results), settings.number_of_output)):
        title = results[i]["title"]
        url = results[i]["url"]
        content = results[i]["content"]
        res.append("{%s|xdg-open %s|%%C%s%%%%L%s}" % (title, url, url, content))

    return ''.join(res)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("query")
    parser.add_argument("--number_of_output", default=2, type=int)
    settings = parser.parse_args()

    print(searx(settings.query, settings))
