# hanaru

hanaru provides minimal API to get beatmaps and metadata. It's based on [drogon][1], which makes it really fast.<br>
It's also caches every beatmap that has been requested to provide work faster, and cache system can be adjusted in memory usage.

# Current limitations

hanaru cannot handle beatmaps with video, so if you want to download beatmapset with video - you should download them manually

# Login Process

when hanaru log in into your account, you will see a new session named 'Webkit (Safari)' and icon from mobile<br>
because of peppy's underlying user-agent parsing, this is the best choose between anything else (its most uncommon)<br>
please don't be scared when you see this session!

# Build

hanaru only depends on drogon, which can be installed by following this [wiki][2]<br>
it requires g++ or MSVC with C++20 and coroutines supports (CLang doesn't supported by drogon)

you also need curl because of login system<br>
Unix
```
sudo apt-get install curl
```
Windows
```
vcpkg install curl:{triplet}
```
where `{triplet}` is `x{system_bits}-windows`

# Optionals

hanaru allows you to specify `maximum_cache_size` in `config.json` in order to reduce memory usage
```json
"maximum_cache_size": 128
```
please keep in mind that LRU cache won't be cleaned until this number, so memory will constantly in use!

hanaru also allows you to specify amount of required free space on hard drive
```json
"required_free_space": 5120 // In megabytes
```
by default `required_free_space` is 5 GB, which optimal for HDD, but might be low if you running this on SDD (o.o)<br>
minimal is 1 GB (or 1024), because drogon can left some other stuff in `uploads` folder<br>
please note that this value cannot be precisely verified, since the value is taken only at the start of the program<br>
so settings this value to something like 25 GB might be good if this enough, if not - please don't be greedy

# Compatability
hanaru uses own JSON structure for `/s/` and `/b/` routes, which will be copied to [Aru][3] later<br>
also hanaru can be used with same database as uses [shiro][4]

shiro can connect to hanaru<br>

# Rate limiting
hanaru uses token bucket system to rate limit requests, with 600 tokens and refresh rate at 10 tokens per second<br>
`/s/` and `/b/` routes consumes 1 token if data in database, and 11 if it downloaded from osu! servers (which will upper limit of osu! API tokens)<br>
`/d/` route consumes 1 token if data in cache, 21 token if data loaded from disk and 61 token if data loaded from osu! server

please note that this rate limit works for the entire system, so if you download a lot of maps, only `/s/` and `/b/` routes will be available

# Error handling
hanaru allow you to don't worry about huge and unrelated error handling systems<br>
for example, if everything good in `/d/` route, then you will get osz file and 200 response<br>
but if something gone wrong - status code will be 400 or 500<br>
here's list of all exceptions:
- 401 - downloader was unauthorized (user might delete our session)
- 404 - beatmapset doesn't exist
- 422 - osu! servers returns invalid osz file
- 423 - downloading disabled after trying to sanitize
- 429 - you hit rate limit, please try again after 10 seconds (this also might be our downloader rate limited by peppy's system)
- 503 - osu! server didn't respond

in `/b/` and `/s/` routes on error you will receive status code, other than 200 and empty JSON object or array (like original osu! API)<br>
currently theres only 404 may occur, or global 500 error, so don't try to parse JSON before checking status code

[1]: https://github.com/drogonframework/drogon
[2]: https://github.com/drogonframework/drogon/wiki/ENG-02-Installation
[3]: https://github.com/Rynnya/Aru
[4]: https://github.com/Rynnya/shiro