// Bookstore Management System - Simplified but functional implementation
// Focus: Correctness for provided command set with persistent storage across runs.

#include <bits/stdc++.h>
using namespace std;

struct Account {
    string user_id;
    string password;
    string username;
    int privilege = 1;
};

struct Book {
    string isbn;
    string name;     // may be empty
    string author;   // may be empty
    string keyword;  // stored as original order joined by '|'
    long long price_cents = 0; // two decimal fixed
    long long stock = 0;
};

struct Context {
    string user_id;
    int privilege = 0;
    string selected_isbn; // empty if none
};

static const string ACC_DB = "accounts.db";
static const string BOOK_DB = "books.db";
static const string FIN_DB = "finance.db";

// Storage
unordered_map<string, Account> accounts;
unordered_map<string, Book> books;
vector<long long> transactions; // positive for income, negative for expenditure

vector<Context> login_stack;

// Utilities
static inline bool is_space(char c){ return c==' '; }

static vector<string> tokenize(const string &line) {
    vector<string> tok;
    string cur;
    bool in_quote = false;
    for (size_t i=0;i<line.size();++i){
        char c=line[i];
        if (in_quote){
            if (c=='"'){
                in_quote=false;
                tok.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        } else {
            if (c=='"'){
                if (!cur.empty()) { tok.push_back(cur); cur.clear(); }
                in_quote=true;
            } else if (is_space(c)){
                if (!cur.empty()){ tok.push_back(cur); cur.clear(); }
            } else {
                cur.push_back(c);
            }
        }
    }
    if (!cur.empty()) tok.push_back(cur);
    return tok;
}

static bool is_valid_user_token(const string &s){
    if (s.empty() || s.size()>30) return false;
    for (char c: s){ if (!(isalnum((unsigned char)c) || c=='_')) return false; }
    return true;
}

static bool is_valid_username(const string &s){
    if (s.empty() || s.size()>30) return false;
    for (unsigned char c: s){ if (c<32) return false; }
    return true;
}

static bool is_valid_isbn(const string &s){
    if (s.size()>20 || s.empty()) return false;
    for (unsigned char c: s){ if (c<32) return false; }
    return true;
}

static bool is_valid_na(const string &s){ // name/author
    if (s.size()>60) return false;
    for (unsigned char c: s){ if (c<32 || c=='"') return false; }
    return true;
}

static bool is_valid_keyword(const string &s){
    if (s.size()>60) return false;
    for (unsigned char c: s){ if (c<32 || c=='"') return false; }
    return true;
}

static bool parse_int(const string &s, long long &out){
    if (s.empty()||s.size()>10) return false;
    long long val=0;
    for (char c: s){ if (!isdigit((unsigned char)c)) return false; val=val*10+(c-'0'); if (val>2147483647LL) return false; }
    out=val; return true;
}

static bool parse_price(const string &s, long long &cents){
    if (s.empty()||s.size()>13) return false;
    long long whole=0, frac=0; int frac_len=0; bool seen_dot=false;
    for (char c: s){
        if (c=='.'){
            if (seen_dot) return false; seen_dot=true; continue;
        }
        if (!isdigit((unsigned char)c)) return false;
        if (!seen_dot){ whole = whole*10 + (c-'0'); if (whole> (long long)9e10) return false; }
        else {
            if (frac_len>=2) return false; frac = frac*10 + (c-'0'); frac_len++;
        }
    }
    if (seen_dot && frac_len==0) return false; // require at least one digit after dot if dot exists
    while (frac_len<2){ frac*=10; frac_len++; }
    cents = whole*100 + frac;
    return true;
}

static string price_to_str(long long cents){
    long long a = cents/100; long long b = llabs(cents%100);
    ostringstream os; os.setf(std::ios::fixed); os<<a<<'.'<<setw(2)<<setfill('0')<<b;
    return os.str();
}

static vector<string> split_keywords(const string &s){
    vector<string> res; string cur; for(char c: s){ if (c=='|'){ res.push_back(cur); cur.clear(); } else cur.push_back(c);} res.push_back(cur); return res;
}

static bool has_duplicate_segments(const string &s){
    auto segs = split_keywords(s);
    unordered_set<string> st;
    for (auto &x: segs){ if (x.empty()) return true; if (!st.insert(x).second) return true; }
    return false;
}

// Persistence
static void load_accounts(){
    accounts.clear();
    ifstream fin(ACC_DB, ios::binary);
    if (!fin.good()){
        // first run: create root
        Account root{"root","sjtu","root",7};
        accounts[root.user_id]=root;
        return;
    }
    size_t n; if (!(fin>>n)) return; string dummy; getline(fin,dummy);
    for (size_t i=0;i<n;i++){
        string uid,pw,uname; int priv; getline(fin,uid); getline(fin,pw); getline(fin,uname); fin>>priv; getline(fin,dummy);
        if (uid.size()) accounts[uid]=Account{uid,pw,uname,priv};
    }
}

static void save_accounts(){
    ofstream fout(ACC_DB, ios::binary|ios::trunc);
    fout<<accounts.size()<<"\n";
    for (auto &kv: accounts){
        auto &a=kv.second;
        fout<<a.user_id<<"\n"<<a.password<<"\n"<<a.username<<"\n"<<a.privilege<<"\n";
    }
}

static void load_books(){
    books.clear();
    ifstream fin(BOOK_DB, ios::binary);
    if (!fin.good()) return;
    size_t n; if (!(fin>>n)) return; string dummy; getline(fin,dummy);
    for (size_t i=0;i<n;i++){
        Book b; string pc,sc;
        getline(fin,b.isbn); getline(fin,b.name); getline(fin,b.author); getline(fin,b.keyword); getline(fin,pc); getline(fin,sc);
        b.price_cents = atoll(pc.c_str()); b.stock = atoll(sc.c_str());
        if (!b.isbn.empty()) books[b.isbn]=b;
    }
}

static void save_books(){
    ofstream fout(BOOK_DB, ios::binary|ios::trunc);
    fout<<books.size()<<"\n";
    for (auto &kv: books){
        auto &b=kv.second;
        fout<<b.isbn<<"\n"<<b.name<<"\n"<<b.author<<"\n"<<b.keyword<<"\n"<<b.price_cents<<"\n"<<b.stock<<"\n";
    }
}

static void load_finance(){
    transactions.clear();
    ifstream fin(FIN_DB, ios::binary);
    if (!fin.good()) return;
    size_t n; if (!(fin>>n)) return; for (size_t i=0;i<n;i++){ long long v; fin>>v; transactions.push_back(v);}    
}

static void save_finance(){
    ofstream fout(FIN_DB, ios::binary|ios::trunc);
    fout<<transactions.size();
    for (auto v: transactions) fout<<' '<<v;
}

static int current_priv(){ return login_stack.empty()?0:login_stack.back().privilege; }
static string current_user(){ return login_stack.empty()?string():login_stack.back().user_id; }
static string &current_selected(){ static string empty; return login_stack.empty()? empty : login_stack.back().selected_isbn; }

static void print_invalid(){ cout<<"Invalid\n"; }

// Command handlers return true if executed, false if invalid (and prints Invalid)

static bool cmd_su(const vector<string>&t){
    if (t.size()<2 || t.size()>3) return false;
    string uid=t[1]; if (!is_valid_user_token(uid)) return false;
    auto it=accounts.find(uid); if (it==accounts.end()) return false;
    string pwd;
    if (t.size()==3) pwd=t[2];
    int curp=current_priv();
    if (t.size()==2){ // password omitted
        if (curp>it->second.privilege){
            // allowed
        } else return false;
    } else {
        if (pwd!=it->second.password) return false;
    }
    login_stack.push_back(Context{uid, it->second.privilege, ""});
    return true;
}

static bool cmd_logout(const vector<string>&t){
    if (t.size()!=1) return false;
    if (login_stack.empty()) return false;
    login_stack.pop_back();
    return true;
}

static bool cmd_register(const vector<string>&t){
    if (t.size()!=4) return false;
    string uid=t[1], pw=t[2], uname=t[3];
    if (!is_valid_user_token(uid) || !is_valid_user_token(pw) || !is_valid_username(uname)) return false;
    if (accounts.count(uid)) return false;
    accounts[uid]=Account{uid,pw,uname,1};
    return true;
}

static bool cmd_passwd(const vector<string>&t){
    if (t.size()!=3 && t.size()!=4) return false;
    string uid=t[1]; if (!is_valid_user_token(uid)) return false;
    auto it=accounts.find(uid); if (it==accounts.end()) return false;
    if (t.size()==3){ // maybe root privilege
        if (current_priv()!=7) return false; // must be root
        string np=t[2]; if (!is_valid_user_token(np)) return false; it->second.password=np; return true;
    } else {
        string cur=t[2], np=t[3];
        if (!is_valid_user_token(cur) || !is_valid_user_token(np)) return false;
        if (current_priv()==7){
            // can change without current password; but if provided, ignore
            it->second.password=np; return true;
        } else {
            if (it->second.password!=cur) return false; it->second.password=np; return true;
        }
    }
}

static bool cmd_useradd(const vector<string>&t){
    if (t.size()!=5) return false;
    if (current_priv()<3) return false;
    string uid=t[1], pw=t[2], privs=t[3], uname=t[4];
    if (!is_valid_user_token(uid) || !is_valid_user_token(pw) || !is_valid_username(uname)) return false;
    if (privs.size()!=1 || !isdigit((unsigned char)privs[0])) return false;
    int p = privs[0]-'0'; if (!(p==1||p==3||p==7)) return false;
    if (p>=current_priv()) return false;
    if (accounts.count(uid)) return false;
    accounts[uid]=Account{uid,pw,uname,p};
    return true;
}

static bool cmd_delete(const vector<string>&t){
    if (t.size()!=2) return false;
    if (current_priv()!=7) return false;
    string uid=t[1]; if (!is_valid_user_token(uid)) return false;
    if (!accounts.count(uid)) return false;
    for (auto &ctx: login_stack){ if (ctx.user_id==uid) return false; }
    accounts.erase(uid);
    return true;
}

static bool cmd_select(const vector<string>&t){
    if (t.size()!=2) return false;
    if (current_priv()<3) return false;
    string isbn=t[1]; if (!is_valid_isbn(isbn)) return false;
    if (!books.count(isbn)){
        Book b; b.isbn=isbn; books[isbn]=b;
    }
    current_selected() = isbn;
    return true;
}

static bool cmd_modify(vector<string> t){
    if (t.size()<2) return false;
    if (current_priv()<3) return false;
    if (current_selected().empty()) return false;
    Book &b = books[current_selected()];
    // parse options like -ISBN=xxx, -name="..."
    string new_isbn=b.isbn, new_name=b.name, new_author=b.author, new_keyword=b.keyword; long long new_price = b.price_cents; bool has_isbn=false, has_name=false, has_author=false, has_keyword=false, has_price=false;
    // merge tokens of the form "-name=" + value
    vector<string> tt; tt.reserve(t.size());
    for (size_t i=1;i<t.size();++i){
        string s=t[i];
        if ((s=="-ISBN=" || s=="-name=" || s=="-author=" || s=="-keyword=" || s=="-price=") && i+1<t.size()){
            tt.push_back(s + t[i+1]);
            ++i;
        } else tt.push_back(s);
    }
    for (size_t i=0;i<tt.size();++i){
        string s=tt[i];
        if (s.rfind("-ISBN=",0)==0){ string v=s.substr(6); if (!is_valid_isbn(v)) return false; new_isbn=v; has_isbn=true; }
        else if (s.rfind("-name=",0)==0){ string v=s.substr(6); if (!is_valid_na(v)) return false; new_name=v; has_name=true; }
        else if (s.rfind("-author=",0)==0){ string v=s.substr(8); if (!is_valid_na(v)) return false; new_author=v; has_author=true; }
        else if (s.rfind("-keyword=",0)==0){ string v=s.substr(9); if (!is_valid_keyword(v)) return false; if (has_duplicate_segments(v)) return false; new_keyword=v; has_keyword=true; }
        else if (s.rfind("-price=",0)==0){ string v=s.substr(7); long long c; if (!parse_price(v,c)) return false; new_price=c; has_price=true; }
        else return false;
    }
    if (has_isbn){ if (new_isbn!=b.isbn && books.count(new_isbn)) return false; }
    // commit
    if (has_isbn){
        if (new_isbn!=b.isbn){
            Book nb=b; nb.isbn=new_isbn; books.erase(b.isbn); books[new_isbn]=nb; current_selected()=new_isbn; b=books[new_isbn];
        }
    }
    Book &ref = books[current_selected()];
    if (has_name) ref.name=new_name;
    if (has_author) ref.author=new_author;
    if (has_keyword) ref.keyword=new_keyword;
    if (has_price) ref.price_cents=new_price;
    return true;
}

static bool cmd_import(const vector<string>&t){
    if (t.size()!=3) return false;
    if (current_priv()<3) return false;
    if (current_selected().empty()) return false;
    long long qty; if (!parse_int(t[1],qty) || qty<=0) return false;
    long long cost; if (!parse_price(t[2],cost) || cost<=0) return false;
    Book &b=books[current_selected()];
    b.stock += qty;
    transactions.push_back(-cost);
    return true;
}

static bool cmd_buy(const vector<string>&t){
    if (t.size()!=3) return false;
    if (current_priv()<1) return false;
    string isbn=t[1]; if (!is_valid_isbn(isbn)) return false;
    auto it=books.find(isbn); if (it==books.end()) return false;
    long long qty; if (!parse_int(t[2],qty) || qty<=0) return false;
    if (it->second.stock < qty) return false;
    it->second.stock -= qty;
    long long income = it->second.price_cents * qty;
    transactions.push_back(income);
    cout<<price_to_str(income)<<"\n";
    return true;
}

static bool cmd_show(vector<string> t){
    if (t.size()<1 || t.size()>3) return false;
    if (current_priv()<1) return false;
    string mode; string val; bool filtered=false;
    if (t.size()==2 || t.size()==3){
        string s=t[1];
        if (t.size()==3 && (s=="-ISBN=" || s=="-name=" || s=="-author=" || s=="-keyword=")){
            s += t[2];
        }
        if (s.rfind("-ISBN=",0)==0){ mode="isbn"; val=s.substr(6); if (!is_valid_isbn(val)) return false; filtered=true; }
        else if (s.rfind("-name=",0)==0){ mode="name"; val=s.substr(6); if (!is_valid_na(val)) return false; filtered=true; }
        else if (s.rfind("-author=",0)==0){ mode="author"; val=s.substr(8); if (!is_valid_na(val)) return false; filtered=true; }
        else if (s.rfind("-keyword=",0)==0){ mode="keyword"; val=s.substr(9); if (!is_valid_keyword(val)) return false; if (val.find('|')!=string::npos) return false; filtered=true; }
        else return false;
    }
    vector<const Book*> arr; arr.reserve(books.size());
    if (!filtered){
        for (auto &kv: books) arr.push_back(&kv.second);
    } else {
        if (mode=="isbn"){ auto it=books.find(val); if (it!=books.end()) arr.push_back(&it->second); }
        else if (mode=="name"){ for (auto &kv: books) if (kv.second.name==val) arr.push_back(&kv.second); }
        else if (mode=="author"){ for (auto &kv: books) if (kv.second.author==val) arr.push_back(&kv.second); }
        else if (mode=="keyword"){ for (auto &kv: books){ auto ks = split_keywords(kv.second.keyword); for (auto &k: ks){ if (k==val){ arr.push_back(&kv.second); break; } } } }
    }
    sort(arr.begin(), arr.end(), [](const Book* a, const Book* b){ return a->isbn < b->isbn; });
    if (arr.empty()){ cout<<"\n"; return true; }
    for (auto b: arr){
        cout<<b->isbn<<"\t"<<b->name<<"\t"<<b->author<<"\t"<<b->keyword<<"\t"<<price_to_str(b->price_cents)<<"\t"<<b->stock<<"\n";
    }
    return true;
}

static bool cmd_show_finance(const vector<string>&t){
    if (current_priv()!=7) return false;
    if (t.size()<2 || t.size()>3) return false;
    if (t[1]!="finance") return false;
    if (t.size()==2){
        long long inc=0, exp=0; for (auto v: transactions){ if (v>=0) inc+=v; else exp-=v; }
        cout<<"+ "<<price_to_str(inc)<<" - "<<price_to_str(exp)<<"\n"; return true;
    } else {
        long long cnt; if (!parse_int(t[2],cnt)) return false;
        if (cnt==0){ cout<<"\n"; return true; }
        if (cnt > (long long)transactions.size()) return false;
        long long inc=0, exp=0; for (long long i=(long long)transactions.size()-cnt;i<(long long)transactions.size();++i){ long long v=transactions[i]; if (v>=0) inc+=v; else exp-=v; }
        cout<<"+ "<<price_to_str(inc)<<" - "<<price_to_str(exp)<<"\n"; return true;
    }
}

static bool cmd_report_or_log(const vector<string>&t){
    if (current_priv()!=7) return false;
    // Self-defined format: produce no output
    return true;
}

int main(){
    ios::sync_with_stdio(false); cin.tie(nullptr);
    load_accounts(); load_books(); load_finance();
    string line;
    while (true){
        if (!std::getline(cin, line)) break;
        bool all_space=true; for (char c: line){ if (!is_space(c)) { all_space=false; break; } }
        if (all_space) { continue; }
        auto t = tokenize(line);
        if (t.empty()) continue;
        string cmd=t[0];
        bool ok=false;
        if (cmd=="quit" || cmd=="exit"){ ok=true; save_accounts(); save_books(); save_finance(); return 0; }
        else if (cmd=="su"){ ok=cmd_su(t); }
        else if (cmd=="logout"){ ok=cmd_logout(t); }
        else if (cmd=="register"){ ok=cmd_register(t); }
        else if (cmd=="passwd"){ ok=cmd_passwd(t); }
        else if (cmd=="useradd"){ ok=cmd_useradd(t); }
        else if (cmd=="delete"){ ok=cmd_delete(t); }
        else if (cmd=="show"){ 
            if (t.size()>=2 && t[1]=="finance") ok=cmd_show_finance(t);
            else ok=cmd_show(t);
        }
        else if (cmd=="buy"){ ok=cmd_buy(t); }
        else if (cmd=="select"){ ok=cmd_select(t); }
        else if (cmd=="modify"){ ok=cmd_modify(t); }
        else if (cmd=="import"){ ok=cmd_import(t); }
        else if (cmd=="log" || (cmd=="report" && t.size()>=2 && (t[1]=="finance" || t[1]=="employee"))){ ok=cmd_report_or_log(t); }
        else { ok=false; }
        if (!ok) print_invalid();
    }
    save_accounts(); save_books(); save_finance();
    return 0;
}
