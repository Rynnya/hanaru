# hanaru

hanaru provides minimal API to get beatmaps and metadata. Its based on [drogon][1], which makes it really fast.<br>
It's also caches every beatmap that has been requested to provide work faster, and cache system can be adjusted in memory usage.
<!-- TODO: Might add some test here? -->

# Current limitations

hanaru cannot handle beatmaps with video, so if you want to download beatmapset with video - you should download them manually

# Build

hanaru only depends on drogon, which can be installed by following this [wiki][2]

# Optionals

hanaru allows you to disable caching, but its very unlikely, as it will always read beatmaps from disk, which might be slow<br>
```
-DHANARU_CACHE=ON
```
to counter this option you can specify `beatmapset_timeout`, `max_memory_usage` and `preferred_memory_usage` in `config.json`
```json
"preferred_memory_usage": 6144, // In megabytes, recommended value is `max_memory_usage` / 2
"max_memory_usage": 8192, // In megabytes, please leave at least 2 gb for your system!
"beatmapset_timeout": 1200 // In seconds
```
after reaching `preferred_memory_usage` cache system starts to remove more recent beatmaps<br>
if default timeout was 20 minutes, in 'preferred' mode it become 10 minutes (divided by 2)<br>
after reaching `max_memory_usage` cache system doesn't accept beatmaps anymore until memory usage doesn't become lower than `preferred_memory_usage`, and beatmap timeout become 5 minutes if default timeout was 20 (divided by 4)

# Compatability
hanaru fully copies [Aru's][3] json structure<br>
also hanaru can be used with same database as uses [shiro][4]

# Error handling
hanaru allow you to don't worry about huge and unrelated error handling systems<br>
for example, if everything good in /d/ route, then you will get osz file and 200 response<br>
but if something gone wrong - status code will be 400 or 500<br>
here's list of all exceptions:
- 404 - beatmapset doesn't exist
- 422 - osu! servers returns invalid osz file
- 423 - downloading disabled after trying to sanitize
- 429 - you hit rate limit, please try again after 10 seconds
- 503 - osu! server didn't respond

[1]: https://github.com/drogonframework/drogon
[2]: https://github.com/drogonframework/drogon/wiki/ENG-02-Installation
[3]: https://github.com/Rynnya/Aru
[4]: https://github.com/Rynnya/shiro