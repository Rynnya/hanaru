# hanaru

hanaru provides minimal API to get beatmaps and metadata. It's based on [drogon][1], which makes it really fast.<br>
It's also caches every beatmap that has been requested to provide work faster, and cache system can be adjusted in memory usage.
<!-- TODO: Might add some test here? -->

# Current limitations

hanaru cannot handle beatmaps with video, so if you want to download beatmapset with video - you should download them manually

# Build

hanaru only depends on drogon, which can be installed by following this [wiki][2]<br>
it requires g++ or MSVC with C++20 and coroutines supports (CLang doesn't supported by drogon)

# Optionals

hanaru allows you to specify `beatmapset_timeout`, `max_memory_usage` and `preferred_memory_usage` in `config.json` in order to reduce memory usage
```json
"preferred_memory_usage": 6144, // In megabytes, recommended value is `max_memory_usage` / 2
"max_memory_usage": 8192, // In megabytes, please leave at least 2 gb for your system!
"beatmapset_timeout": 1200 // In seconds
```
after reaching `preferred_memory_usage` cache system starts to remove more recent beatmaps<br>
if default timeout was 20 minutes, in 'preferred' mode it become 10 minutes (divided by 2)<br>
after reaching `max_memory_usage` cache system doesn't accept beatmaps anymore until memory usage doesn't become lower than `max_memory_usage`, and beatmap timeout become 5 minutes if default timeout was 20 (divided by 4)

hanaru also allows you to specify amount of required free space on hard drive
```json
"required_free_space": 5120 // In megabytes
```
by default `required_free_space` is 5 GB, which optimal for HDD, but might be low if you running this on SDD (o.o)<br>
minimal is 1 GB (or 1024), because drogon can left some other stuff in `uploads` folder<br>
please note that this value cannot be precisely verified, as this will decrease performance, so check runs every minute<br>
so leave at least around 500 mb above your limit, just in case

# Compatability
hanaru uses own JSON structure for `/s/` and `/b/` routes, which will be copied to [Aru][3] later
also hanaru can be used with same database as uses [shiro][4]

shiro can connect to hanaru through websocket
shiro should run on same machine (because of LocalHostFilter)

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
- 404 - beatmapset doesn't exist
- 422 - osu! servers returns invalid osz file
- 423 - downloading disabled after trying to sanitize
- 429 - you hit rate limit, please try again after 10 seconds
- 500 - exception happend inside hanaru (only websocket)
- 503 - osu! server didn't respond

in `/b/` and `/s/` routes on error you will receive status code, other than 200 and empty JSON object or array (like original osu! API)<br>
currently theres only 404 may occur, or global 500 error, so don't try to parse JSON before checking status code

[1]: https://github.com/drogonframework/drogon
[2]: https://github.com/drogonframework/drogon/wiki/ENG-02-Installation
[3]: https://github.com/Rynnya/Aru
[4]: https://github.com/Rynnya/shiro