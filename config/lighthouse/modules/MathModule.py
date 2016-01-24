import lhapi
import math

class MathModule(lhapi.Module):
  def getResults(self, query):
    ns = vars(math).copy()
    ns['__builtins__'] = None
    try:
      output = str(eval(query, ns))
      return [lhapi.Result(output, '', '', 100)]
    except:
      return []
