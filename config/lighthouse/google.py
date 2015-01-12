#!/usr/bin/env python
"""
Google AJAX Search Module
http://code.google.com/apis/ajaxsearch/documentation/reference.html
Needs Python 2.6 or later
"""
try:
    import json
except ImportError,e:
    import simplejson as json
except ImportError,e:
    print e
    exit()

import sys
import urllib
import logging
import argparse

__author__ = "Kiran Bandla"
__version__ = "0.2"
URL = 'http://ajax.googleapis.com/ajax/services/search/web?'

#Web Search Specific Arguments
#http://code.google.com/apis/ajaxsearch/documentation/reference.html#_fonje_web
#SAFE,FILTER
"""
SAFE
This optional argument supplies the search safety level which may be one of:
    * safe=active - enables the highest level of safe search filtering
    * safe=moderate - enables moderate safe search filtering (default)
    * safe=off - disables safe search filtering
"""
SAFE_ACTIVE     = "active"
SAFE_MODERATE   = "moderate"
SAFE_OFF        = "off"

"""
FILTER
This optional argument controls turning on or off the duplicate content filter:

    * filter=0 - Turns off the duplicate content filter
    * filter=1 - Turns on the duplicate content filter (default)

"""
FILTER_OFF  = 0
FILTER_ON   = 1

#Standard URL Arguments
#http://code.google.com/apis/ajaxsearch/documentation/reference.html#_fonje_args
"""
RSZ
This optional argument supplies the number of results that the application would like to recieve.
A value of small indicates a small result set size or 4 results.
A value of large indicates a large result set or 8 results. If this argument is not supplied, a value of small is assumed.
"""
RSZ_SMALL = "small"
RSZ_LARGE = "large"

"""
HL
This optional argument supplies the host language of the application making the request.
If this argument is not present then the system will choose a value based on the value of the Accept-Language http header.
If this header is not present, a value of en is assumed.
"""

class pygoogle:
   
    def __init__(self,query,pages=10,hl='en',log_level=logging.INFO):
        self.pages = pages          #Number of pages. default 10
        self.query = query
        self.filter = FILTER_ON     #Controls turning on or off the duplicate content filter. On = 1.
        self.rsz = RSZ_LARGE        #Results per page. small = 4 /large = 8
        self.safe = SAFE_OFF        #SafeBrowsing -  active/moderate/off
        self.hl = hl                #Defaults to English (en)
        self.__setup_logging(level=log_level)
       
    def __setup_logging(self, level):
        logger = logging.getLogger('pygoogle')
        logger.setLevel(level)
        handler = logging.StreamHandler(sys.stdout)
        handler.setFormatter(logging.Formatter('%(module)s %(levelname)s %(funcName)s| %(message)s'))
        logger.addHandler(handler)
        self.logger = logger

    def __search__(self,print_results=False):
        '''
        returns list of results if successful or False otherwise
        '''
        results = []
        for page in range(0,self.pages):
            rsz = 8
            if self.rsz == RSZ_SMALL:
                rsz = 4
            args = {'q' : self.query,
                    'v' : '1.0',
                    'start' : page*rsz,
                    'rsz': self.rsz,
                    'safe' : self.safe,
                    'filter' : self.filter,    
                    'hl'    : self.hl
                    }
            self.logger.debug('search: "%s" page# : %s'%(self.query, page))
            q = urllib.urlencode(args)
            search_results = urllib.urlopen(URL+q)
            data = json.loads(search_results.read())
            if not data.has_key('responseStatus'):
                self.logger.error('response does not have a responseStatus key')
                continue
            if data.get('responseStatus') != 200:
                self.logger.debug('responseStatus is not 200')
                self.logger.error('responseDetails : %s'%(data.get('responseDetails', None)))
                continue
            if print_results:
                if data.has_key('responseData') and data['responseData'].has_key('results'):
                    for result in  data['responseData']['results']:
                        if result:
                            print '[%s]'%(urllib.unquote(result['titleNoFormatting']))
                            print result['content'].strip("<b>...</b>").replace("<b>",'').replace("</b>",'').replace("&#39;","'").strip()
                            print urllib.unquote(result['unescapedUrl'])+'\n'                
                else:
                    # no responseData key was found in 'data'
                    self.logger.error('no responseData key found in response. very unusal')
            results.append(data)
        return results
   
    def search(self):
        """Returns a dict of Title/URLs"""
        results = {}
        search_results = self.__search__()
        if not search_results:
            self.logger.info('No results returned')
            return results
        for data in search_results:
            if data.has_key('responseData') and data['responseData'].has_key('results'):
                for result in data['responseData']['results']:
                    if result and result.has_key('titleNoFormatting'):
                        title = urllib.unquote(result['titleNoFormatting'])
                        results[title] = urllib.unquote(result['unescapedUrl'])
            else:
                self.logger.error('no responseData key found in response')
                self.logger.error(data)
        return results

    def search_page_wise(self):
        """Returns a dict of page-wise urls"""
        results = {}
        for page in range(0,self.pages):
            args = {'q' : self.query,
                    'v' : '1.0',
                    'start' : page,
                    'rsz': RSZ_LARGE,
                    'safe' : SAFE_OFF,
                    'filter' : FILTER_ON,    
                    }
            q = urllib.urlencode(args)
            search_results = urllib.urlopen(URL+q)
            data = json.loads(search_results.read())
            urls = []
            if data.has_key('responseData') and data['responseData'].has_key('results'):
                for result in  data['responseData']['results']:
                    if result and result.has_key('unescapedUrl'):
                        url = urllib.unquote(result['unescapedUrl'])
                        urls.append(url)            
            else:
                self.logger.error('no responseData key found in response')
            results[page] = urls
        return results
       
    def get_urls(self):
        """Returns list of result URLs"""
        results = []
        search_results = self.__search__()
        if not search_results:
            self.logger.info('No results returned')
            return results
        for data in search_results:
            if data and data.has_key('responseData') and data['responseData']['results']:
                for result in  data['responseData']['results']:
                    if result:
                        results.append(urllib.unquote(result['unescapedUrl']))
        return results

    def get_result_count(self):
        """Returns the number of results"""
        temp = self.pages
        self.pages = 1
        result_count = 0
        search_results = self.__search__()
        if not search_results:
            return 0
        try:
            result_count = search_results[0]
            if not isinstance(result_count, dict):
                return 0
            result_count = result_count.get('responseData', None)
            if result_count:
                if result_count.has_key('cursor') and result_count['cursor'].has_key('estimatedResultCount'):
                    return result_count['cursor']['estimatedResultCount']
            return 0
        except Exception,e:
            self.logger.error(e)
        finally:
            self.pages = temp
        return result_count
       
    def display_results(self):
        """Prints results (for command line)"""
        self.__search__(True)

def main():
    parser = argparse.ArgumentParser(description='A simple Google search module for Python')
    parser.add_argument('-v', '--verbose', dest='verbose', action='store_true', default=False, help='Verbose mode')
    parser.add_argument('-p', '--pages', dest='pages', action='store', default=1, help='Number of pages to return. Max 10')
    parser.add_argument('-hl', '--language', dest='language', action='store', default='en', help="language. default is 'en'")
    parser.add_argument('query', nargs='*', default=None)
    args = parser.parse_args()
    query = ' '.join(args.query)
    log_level = logging.INFO
    if args.verbose:
        log_level = logging.DEBUG
    if not query:
        parser.print_help()
        exit()
    search = pygoogle( log_level=log_level, query=query, pages=args.pages, hl=args.language)
    search.display_results()

if __name__ == "__main__":
    main()

