var idx = lunr.Index.load(search_index);
var txt = document.getElementById("search_text");
var search_what = document.getElementById("search_what");
var sb = document.getElementById("search_button");
var display = document.getElementById("search_results");
var root = document.body.dataset.root;

for (const [key, value] of Object.entries(search_type)) {
  var opt = document.createElement("option");
  opt.value = key;
  opt.innerHTML = value;
  search_what.appendChild(opt);
}

var do_search = function () {
  var what = txt.value;
  if (what) {
    display.innerHTML = '';
    var hq = function(q) {
      q.term(lunr.tokenizer(what), {fields: ['name', 'text'], presence: lunr.Query.presence.REQUIRED});
      var sw = search_what.value;
      if (sw != '')
        q.term(sw, {fields: ['type'], presence: lunr.Query.presence.REQUIRED});
    };
    var results = idx.query(hq);
    if (results.length > 0) {
      var more = results.length > 10;
      if (more)
        results = results.slice(0, 10);
      const list = document.createElement("ul");
      display.appendChild(list);
      results.forEach(function(item) {
        const data = search_data[item.ref];
        const li = document.createElement("li");
        const a = document.createElement("a");
        a.href = root + data.url;
        const n = document.createElement("span");
        n.innerText = data.name;
        a.appendChild(n);
        const t = document.createElement("span");
        t.innerText = "(" + search_type[data.type] + ")";
        a.appendChild(t);
        li.appendChild(a);
        list.appendChild(li);
      });
      if (more) {
        const li = document.createElement("li");
        li.appendChild(document.createTextNode("(only first 10 items displayed)"));
        list.appendChild(li);
      }
    } else {
      display.appendChild(document.createTextNode("No matching result"));
    }
  }
}

txt.addEventListener('keydown', function onEvent(event) {
    if (event.key === "Enter") {
        do_search();
        return false;
    }
});

sb.addEventListener('click', function() {
  do_search();
  return false;
});
